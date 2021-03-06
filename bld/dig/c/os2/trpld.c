/****************************************************************************
*
*                            Open Watcom Project
*
*    Portions Copyright (c) 1983-2002 Sybase, Inc. All Rights Reserved.
*
*  ========================================================================
*
*    This file contains Original Code and/or Modifications of Original
*    Code as defined in and that are subject to the Sybase Open Watcom
*    Public License version 1.0 (the 'License'). You may not use this file
*    except in compliance with the License. BY USING THIS FILE YOU AGREE TO
*    ALL TERMS AND CONDITIONS OF THE LICENSE. A copy of the License is
*    provided with the Original Code and Modifications, and is also
*    available at www.sybase.com/developer/opensource.
*
*    The Original Code and all software distributed under the License are
*    distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
*    EXPRESS OR IMPLIED, AND SYBASE AND ALL CONTRIBUTORS HEREBY DISCLAIM
*    ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR
*    NON-INFRINGEMENT. Please see the License for the specific language
*    governing rights and limitations under the License.
*
*  ========================================================================
*
* Description:  Trap module loader for OS/2.
*
****************************************************************************/


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define INCL_DOSMODULEMGR
#define INCL_DOSMISC
#include <wos2.h>
#include "trptypes.h"
#include "trpld.h"
#include "trpsys.h"
#include "tcerr.h"


#ifdef _M_I86
#define GET_PROC_ADDRESS(m,s,f)   (DosGetProcAddr( m, "#" #s, (PFN FAR *)&f ) == 0)
#define LOAD_MODULE(n,m)          (DosLoadModule( NULL, 0, (char *)n, &m ) != 0 )
#else
#define GET_PROC_ADDRESS(m,s,f)   (DosQueryProcAddr( m, s, NULL, (PFN FAR *)&f ) == 0)
#define LOAD_MODULE(n,m)          (DosLoadModule( NULL, 0, n, &m ) != 0 )
#endif

#define LOW(c)      ((c) | 0x20)

static HMODULE          TrapFile = 0;
static trap_fini_func   *FiniFunc = NULL;

static TRAPENTRY_FUNC_PTR( TellHandles );
static TRAPENTRY_FUNC_PTR( TellHardMode );

bool IsTrapFilePumpingMessageQueue( void )
{
    return( TRAPENTRY_PTR_NAME( TellHandles ) != NULL );
}

bool TrapTellHandles( HAB hab, HWND hwnd )
{
    if( TRAPENTRY_PTR_NAME( TellHandles ) == NULL )
        return( false );
    TRAPENTRY_PTR_NAME( TellHandles )( hab, hwnd );
    return( true );
}


char TrapTellHardMode( char hard )
{
    if( TRAPENTRY_PTR_NAME( TellHardMode ) == NULL )
        return( 0 );
    return( TRAPENTRY_PTR_NAME( TellHardMode )( hard ) );
}

void KillTrap( void )
{
    ReqFunc = NULL;
    TRAPENTRY_PTR_NAME( TellHandles ) = NULL;
    TRAPENTRY_PTR_NAME( TellHardMode ) = NULL;
    if( FiniFunc != NULL ) {
        FiniFunc();
        FiniFunc = NULL;
    }
    if( TrapFile != 0 ) {
        DosFreeModule( TrapFile );
        TrapFile = 0;
    }
}

char *LoadTrap( const char *parms, char *buff, trap_version *trap_ver )
{
    unsigned            len;
    const char          *ptr;
    trap_init_func      *init_func;
    char                trpfile[CCHMAXPATH];
#ifndef _M_I86
    char                trpname[CCHMAXPATH];
#endif

    if( parms == NULL || *parms == '\0' )
        parms = "std";
    len = 0;
    for( ptr = parms; *ptr != '\0'; ++ptr ) {
        if( *ptr == TRAP_PARM_SEPARATOR ) {
            ptr++;
            break;
        }
        trpfile[len++] = *ptr;
    }
#ifdef _M_I86
    if( LOW( trpfile[0] ) == 's' && LOW( trpfile[1] ) == 't'
      && LOW( trpfile[2] ) == 'd' && trpfile[3] == '\0' ) {
        unsigned        version;
        char            os2ver;

        DosGetVersion( (PUSHORT)&version );
        os2ver = version >> 8;
        if( os2ver >= 20 ) {
            trpfile[len++] = '3';
            trpfile[len++] = '2';
        } else {
            trpfile[len++] = '1';
            trpfile[len++] = '6';
        }
    }
#endif
#ifdef USE_FILENAME_VERSION
    trpfile[len++] = ( USE_FILENAME_VERSION / 10 ) + '0';
    trpfile[len++] = ( USE_FILENAME_VERSION % 10 ) + '0';
#endif
    trpfile[len] = '\0';
#ifndef _M_I86
    /* To prevent conflicts with the 16-bit DIP DLLs, the 32-bit versions have the "D32"
     * extension. We will search for them along the PATH (not in LIBPATH);
     */
    strcpy( trpname, trpfile );
    strcat( trpname, ".D32" );
    _searchenv( trpname, "PATH", trpfile );
    if( *trpfile == '\0' ) {
        sprintf( buff, TC_ERR_CANT_LOAD_TRAP, trpname );
        return( buff );
    }
#endif
    if( LOAD_MODULE( trpfile, TrapFile ) ) {
        sprintf( buff, TC_ERR_CANT_LOAD_TRAP, trpfile );
        return( buff );
    }
    strcpy( buff, TC_ERR_WRONG_TRAP_VERSION );
    if( GET_PROC_ADDRESS( TrapFile, 1, init_func )
      && GET_PROC_ADDRESS( TrapFile, 2, FiniFunc )
      && GET_PROC_ADDRESS( TrapFile, 3, ReqFunc ) ) {
        if( !GET_PROC_ADDRESS( TrapFile, 4, TRAPENTRY_PTR_NAME( TellHandles ) ) ) {
            TRAPENTRY_PTR_NAME( TellHandles ) = NULL;
        }
        if( !GET_PROC_ADDRESS( TrapFile, 5, TRAPENTRY_PTR_NAME( TellHardMode ) ) ) {
            TRAPENTRY_PTR_NAME( TellHardMode ) = NULL;
        }
        parms = ptr;
        *trap_ver = init_func( parms, buff, trap_ver->remote );
        if( buff[0] == '\0' ) {
            if( TrapVersionOK( *trap_ver ) ) {
                TrapVer = *trap_ver;
                return( NULL );
            }
            strcpy( buff, TC_ERR_WRONG_TRAP_VERSION );
        }
    }
    KillTrap();
    return( buff );
}
