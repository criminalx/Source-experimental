
#ifdef _WIN32
	#include "../sphere/ntservice.h"	// g_Service
#else
	#include "../sphere/UnixTerminal.h"
    #include <sys/resource.h>   // for setpriority
#endif

#if defined(_WIN32) && !defined(_DEBUG)
	#include "../common/crashdump/crashdump.h"
#endif

#include "../common/sphere_library/CSAssoc.h"
#include "../common/CException.h"
#include "../common/sphere_library/CSFileList.h"
#include "../common/CTextConsole.h"
#include "../common/CLog.h"
#include "../common/sphereversion.h"	// sphere version
#include "../network/network.h"
#include "../sphere/ProfileTask.h"
#include "../sphere/ntwindow.h"
#include "chars/CChar.h"
#include "clients/CAccount.h"
#include "clients/CClient.h"
#include "items/CItemShip.h"
#include "CWorld.h"
#include "spheresvr.h"
#include "triggers.h"
#include "CScriptProfiler.h"
#include "CServer.h"
#include <cstdio>



////////////////////////////////////////////////////////////////////////////////////////
// -CServer

CServer::CServer() : CServerDef( SPHERE_TITLE, CSocketAddressIP( SOCKET_LOCAL_ADDRESS ))
{
	SetExitFlag(0);
	m_fResyncPause = false;
	m_fResyncRequested = nullptr;

	m_iAdminClients = 0;

	m_timeShutdown = 0;

	m_fConsoleTextReadyFlag = false;

	// we are in start up mode. // IsLoading()
	SetServerMode( SERVMODE_PreLoadingINI );

	memset(m_PacketFilter, 0, sizeof(m_PacketFilter));
	memset(m_OutPacketFilter, 0, sizeof(m_OutPacketFilter));
}

CServer::~CServer()
{
}

void CServer::SetSignals( bool fMsg )
{
	// We have just started or we changed Secure mode.

	// set_terminate(  &Exception_Terminate );
	// set_unexpected( &Exception_Unexpected );

	SetUnixSignals(g_Cfg.m_fSecure);
#ifndef _WIN32
	if ( g_Cfg.m_fSecure )
	{
		g_Log.Event( (IsLoading() ? 0 : LOGL_EVENT) | LOGM_INIT, "Signal handlers installed.\n" );
	}
	else
	{
		g_Log.Event( (IsLoading() ? 0 : LOGL_EVENT) | LOGM_INIT, "Signal handlers UNinstalled.\n" );
	}
#endif

	if ( fMsg && !IsLoading() )
	{
		g_World.Broadcast( g_Cfg.m_fSecure ?
			"The world is now running in SECURE MODE" :
			"WARNING: The world is NOT running in SECURE MODE" );
	}
}

bool CServer::SetProcessPriority(int iPriorityLevel)
{
    bool fSuccess;
#ifdef _WIN32
    HANDLE hCurrentProcess = GetCurrentProcess();
    DWORD dwPri;
    switch (iPriorityLevel)
    {
        case -1:    dwPri = BELOW_NORMAL_PRIORITY_CLASS;    break;
        default:
        case 0:     dwPri = NORMAL_PRIORITY_CLASS;          break;
        case 1:     dwPri = ABOVE_NORMAL_PRIORITY_CLASS;    break;
        case 2:     dwPri = HIGH_PRIORITY_CLASS;            break;
    }
    fSuccess = (bool)SetPriorityClass(hCurrentProcess, dwPri);
#else
    int iPri;
    switch (iPriorityLevel)
    {
        case -1:    iPri = 7;   break;
        default:
        case 0:     iPri = 0;   break;
        case 1:     iPri = -8;  break;
        case 2:     iPri = -15; break;
    }
    // This will FAIL if you aren't running Sphere as root, or if root didn't set for this process the s bit (chmod +x).
    if ( setpriority(PRIO_PROCESS, 0, iPri) == -1 )
        fSuccess = false;
    else
        fSuccess = true;
    //needed option for mac?
#endif
    return fSuccess;
}

SERVMODE_TYPE CServer::GetServerMode() const
{
    return m_iModeCode.load(std::memory_order_acquire);
}

void CServer::SetServerMode( SERVMODE_TYPE mode )
{
	ADDTOCALLSTACK("CServer::SetServerMode");
	m_iModeCode.store(mode, std::memory_order_release);
#ifdef _WIN32
    g_NTWindow.SetWindowTitle();
#endif
}

bool CServer::IsValidBusy() const
{
    // We might appear to be stopped but it's really ok ?
    // ?
    switch ( GetServerMode() )
    {
        case SERVMODE_Saving:
            if ( g_World.IsSaving() )
                return true;
            break;
        case SERVMODE_Loading:
        case SERVMODE_GarbageCollection:
        case SERVMODE_RestockAll:	// these may look stuck but are not.
            return true;
        default:
            return false;
    }
    return false;
}

int CServer::GetExitFlag() const
{
    return m_iExitFlag.load(std::memory_order_acquire);
}

void CServer::SetExitFlag(int iFlag)
{
    ADDTOCALLSTACK("CServer::SetExitFlag");
    if ( GetExitFlag() )
        return;
    m_iExitFlag.store(iFlag, std::memory_order_release);
}

bool CServer::IsLoading() const
{
    return ( m_fResyncPause || (GetServerMode() > SERVMODE_Run) );
}

bool CServer::IsResyncing() const
{
    return m_fResyncPause || (GetServerMode() == SERVMODE_ResyncLoad);
}

void CServer::Shutdown( int64 iMinutes ) // If shutdown is initialized
{
	ADDTOCALLSTACK("CServer::Shutdown");
	if ( iMinutes == 0 )
	{
		if ( ! (m_timeShutdown > 0) )
			return;
		m_timeShutdown = 0;
		g_World.Broadcast( g_Cfg.GetDefaultMsg( DEFMSG_MSG_SERV_SHUTDOWN_CANCEL ) );
		return;
	}

	if ( iMinutes < 0 )
	{
		iMinutes = g_World.GetTimeDiff( m_timeShutdown ) / ( 60 * MSECS_PER_SEC);
	}
	else
	{
		m_timeShutdown = g_World.GetCurrentTime().GetTimeRaw() + ( iMinutes * 60 * MSECS_PER_SEC);
	}

	g_World.Broadcastf(g_Cfg.GetDefaultMsg( DEFMSG_MSG_SERV_SHUTDOWN ), iMinutes);
}

void CServer::SysMessage( lpctstr pszMsg ) const
{
	// Print just to the main console.
	if ( !pszMsg )
		return;

#ifdef _WIN32
    g_NTWindow.AddConsoleOutput(new ConsoleOutput(pszMsg));
#else
    g_UnixTerminal.AddConsoleOutput(new ConsoleOutput(pszMsg));
#endif
}

void CServer::SysMessage(ConsoleOutput *pszMsg) const
{
    // Print just to the main console.
    if ( !pszMsg )
        return;

#ifdef _WIN32
    g_NTWindow.AddConsoleOutput(pszMsg);
#else
    g_UnixTerminal.AddConsoleOutput(pszMsg);
#endif
}

void CServer::PrintTelnet( lpctstr pszMsg ) const
{
	if ( ! m_iAdminClients )
		return;

	ClientIterator it;
	for (CClient* pClient = it.next(); pClient != nullptr; pClient = it.next())
	{
		if (( pClient->GetConnectType() == CONNECT_TELNET ) && pClient->GetAccount() )
		{
			if ( !pClient->GetAccount()->IsPriv(PRIV_TELNET_SHORT) )	// this client accepts broadcasts
				pClient->SysMessage(pszMsg);
		}
	}
}

void CServer::PrintStr( lpctstr pszMsg ) const
{
	// print to all consoles.
	SysMessage( pszMsg );
	PrintTelnet( pszMsg );
}

void CServer::PrintStr(ConsoleTextColor iColor, lpctstr pMsg) const
{
    // print to all consoles.
    SysMessage(new ConsoleOutput(iColor, pMsg));
    PrintTelnet(pMsg);
}

void CServer::PrintOutput(ConsoleOutput * pOutput) const
{
    SysMessage(pOutput);
}

ssize_t CServer::PrintPercent( ssize_t iCount, ssize_t iTotal )
{
	ADDTOCALLSTACK("CServer::PrintPercent");
	if ( iTotal <= 0 )
		return 100;

	int iPercent = (int)IMulDivLL( iCount, 100, iTotal );
	tchar *pszTemp = Str_GetTemp();
	sprintf(pszTemp, "%d%%", iPercent);
	size_t len = strlen(pszTemp);

	PrintTelnet(pszTemp);

#ifndef _WIN32
	if ( g_UnixTerminal.isColorEnabled() )
	{
		SysMessage(pszTemp);
#endif
		while ( len > 0 )	// backspace it
		{
			PrintTelnet("\x08");
#ifndef _WIN32
 			SysMessage("\x08");
#endif
			--len;
		}
#ifndef _WIN32
	}
#endif

#ifdef _WIN32
    g_NTWindow.SetWindowTitle(pszTemp);
	g_NTService.OnTick();
#endif
	return iPercent;
}

int64 CServer::GetAgeHours() const
{
	ADDTOCALLSTACK("CServer::GetAgeHours");
	return (g_World.GetCurrentTime().GetTimeRaw() / (60 * 60 * MSECS_PER_SEC));
}

lpctstr CServer::GetStatusString( byte iIndex ) const
{
	ADDTOCALLSTACK("CServer::GetStatusString");
	// NOTE: The key names should match those in CServerDef::r_LoadVal
	// A ping will return this as well.
	// 0 or 0x21 = main status.

	tchar * pTemp = Str_GetTemp();
	size_t iClients = StatGet(SERV_STAT_CLIENTS);
	int64 iHours = GetAgeHours() / 24;

	switch ( iIndex )
	{
		case 0x21:	// '!'
			// typical (first time) poll response.
			{
				char szVersion[128];
				sprintf(pTemp, SPHERE_TITLE ", Name=%s, Port=%d, Ver=" SPHERE_VERSION ", TZ=%d, EMail=%s, URL=%s, Lang=%s, CliVer=%s\n",
					GetName(), m_ip.GetPort(), m_TimeZone, static_cast<lpctstr>(m_sEMail), static_cast<lpctstr>(m_sURL), static_cast<lpctstr>(m_sLang),
					m_ClientVersion.WriteClientVer(szVersion));
			}
			break;
		case 0x22: // '"'
			{
			// shown in the INFO page in game.
			sprintf(pTemp, SPHERE_TITLE ", Name=%s, Age=%" PRId64 ", Clients=%" PRIuSIZE_T ", Items=%" PRIuSIZE_T ", Chars=%" PRIuSIZE_T ", Mem=%" PRIuSIZE_T "K\n",
				GetName(), iHours, iClients, StatGet(SERV_STAT_ITEMS), StatGet(SERV_STAT_CHARS), StatGet(SERV_STAT_MEM));
			}
			break;
		case 0x24: // '$'
			// show at startup.
			sprintf(pTemp, "Admin=%s, URL=%s, Lang=%s, TZ=%d\n",
				static_cast<lpctstr>(m_sEMail), static_cast<lpctstr>(m_sURL), static_cast<lpctstr>(m_sLang), m_TimeZone);
			break;
		case 0x25: // '%'
			// ConnectUO Status string
			sprintf(pTemp, SPHERE_TITLE " Items=%" PRIuSIZE_T ", Mobiles=%" PRIuSIZE_T ", Clients=%" PRIuSIZE_T ", Mem=%" PRIuSIZE_T,
				StatGet(SERV_STAT_ITEMS), StatGet(SERV_STAT_CHARS), iClients, StatGet(SERV_STAT_MEM));
			break;
	}

	return pTemp;
}

//*********************************************************

void CServer::ListClients( CTextConsole *pConsole ) const
{
	ADDTOCALLSTACK("CServer::ListClients");
	// Mask which clients we want ?
	// Give a format of what info we want to SHOW ?
	if ( pConsole == nullptr )
		return;

	const CChar *pCharCmd = pConsole->GetChar();
	const CChar *pChar = nullptr;
	const CAccount *pAcc = nullptr;
	tchar *pszMsg = Str_GetTemp();
	tchar *tmpMsg = Str_GetTemp();
	tchar chRank = 0;
	lpcstr pszState = nullptr;
	size_t numClients = 0;

	ClientIterator it;
	for ( const CClient *pClient = it.next(); pClient != nullptr; pClient = it.next() )
	{
		numClients++;
		pChar = pClient->GetChar();
		pAcc = pClient->GetAccount();
		chRank = (pAcc && pAcc->GetPrivLevel() > PLEVEL_Player) ? '+' : '=';

		if ( pChar )
		{
			if ( pCharCmd && !pCharCmd->CanDisturb(pChar) )
				continue;

			sprintf(tmpMsg, "%" PRIx32 ":Acc%c'%s', Char='%s' (IP: %s)\n", pClient->GetSocketID(), chRank, pAcc->GetName(), pChar->GetName(), pClient->GetPeerStr());
		}
		else
		{
			if ( pConsole->GetPrivLevel() < pClient->GetPrivLevel() )
				continue;

			switch ( pClient->GetConnectType() )
			{
				case CONNECT_HTTP:
					pszState = "WEB";
					break;
				case CONNECT_TELNET:
					pszState = "TELNET";
					break;
				default:
					pszState = "NOT LOGGED IN";
					break;
			}

			sprintf(tmpMsg, "%" PRIx32 ":Acc%c'%s' (IP: %s) %s\n", pClient->GetSocketID(), chRank, pAcc ? pAcc->GetName() : "<NA>", pClient->GetPeerStr(), pszState);
		}

		// If we have many clients, SCRIPT_MAX_LINE_LEN may be too short ;) (matex)
		if ( strlen(pszMsg) + strlen(tmpMsg) >= SCRIPT_MAX_LINE_LEN )
		{
			pConsole->SysMessage(pszMsg);
			pszMsg[0] = '\0';
		}
		ASSERT((strlen(pszMsg) + strlen(tmpMsg)) < SCRIPT_MAX_LINE_LEN);
		strcat(pszMsg, tmpMsg);
	}

	if ( numClients <= 0 )
		sprintf(tmpMsg, "%s\n", g_Cfg.GetDefaultMsg(DEFMSG_HL_NO_CLIENT));
	else if ( numClients == 1 )
		sprintf(tmpMsg, "%s\n", g_Cfg.GetDefaultMsg(DEFMSG_HL_ONE_CLIENT));
	else
		sprintf(tmpMsg, "%s %" PRIuSIZE_T "\n", g_Cfg.GetDefaultMsg(DEFMSG_HL_MANY_CLIENTS), numClients);

	pConsole->SysMessage(pszMsg);
	pConsole->SysMessage(tmpMsg);
}

bool CServer::OnConsoleCmd( CSString & sText, CTextConsole * pSrc )
{
	ADDTOCALLSTACK("CServer::OnConsoleCmd");
	// RETURN: false = unsuccessful command.
	size_t len = sText.GetLength();

	// We can't have a command with no length
	if ( len <= 0 )
		return true;

	// Convert first character to lowercase
	tchar low = static_cast<tchar>(tolower(sText[0]));
	bool fRet = true;

	if ((( len > 2 ) || (( len == 2 ) && ( sText[1] != '#' ))) && ( sText[0] != 'd' ))
		goto longcommand;

	switch ( low )
	{
		case '?':
			g_Log.Event(LOGL_EVENT,
				"Available commands:\n"
				"#         Immediate save world (## to save both world and statics)\n"
				"A         Update pending changes on Accounts file\n"
				"B [msg]   Broadcast message to all clients\n"
				"C         List of online Clients (%lu)\n"
				"DA        Dump Areas to external file\n"
				"DUI       Dump Unscripted Items to external file\n"
				"E         Clear internal variables (like script profile)\n"
				"G         Garbage collection\n"
				"H         Hear all that is said (%s)\n"
				"I         View server Information\n"
				"L         Toggle log file (%s)\n"
				"P         Profile Info (%s) (P# to dump to profiler_dump.txt)\n"
				"R         Resync Pause\n"
				"S         Secure mode toggle (%s)\n"
				"STRIP     Dump all script templates to external file, formatted for Axis\n"
				"STRIPTNG  Dump all script templates to external file, formatted for TNG\n"
				"T         List of active Threads\n"
				"U         List used triggers\n"
				"X         Immediate exit the server (X# to save world and statics before exit)\n"
				,
				StatGet(SERV_STAT_CLIENTS),
				g_Log.IsLoggedMask( LOGM_PLAYER_SPEAK ) ? "ON" : "OFF",
				g_Log.IsFileOpen() ? "OPEN" : "CLOSED",
				CurrentProfileData.IsActive() ? "ON" : "OFF",
				g_Cfg.m_fSecure ? "ON" : "OFF"
				);
			break;
		case '#':	//	# - save world, ## - save both world and statics
			{		// Start a syncronous save or finish a background save synchronously
				if ( g_Serv.m_fResyncPause )
					goto do_resync;

				g_World.Save(true);
				if (( len > 1 ) && ( sText[1] == '#' ))
					g_World.SaveStatics();	// ## means
			} break;
		case 'a':
			{
				// Force periodic stuff
				g_Accounts.Account_SaveAll();
				g_Cfg.OnTick(true);
			} break;
		case 'c':	// List all clients on line.
			{
				ListClients( pSrc );
			} break;
		case 'd': // dump
			{
				lpctstr pszKey = sText + 1;
				GETNONWHITESPACE( pszKey );
				switch ( tolower(*pszKey) )
				{
					case 'a': // areas
						pszKey++;	GETNONWHITESPACE( pszKey );
						if ( !g_World.DumpAreas( pSrc, pszKey ) )
						{
                            if (pSrc != this)
                            {
                                pSrc->SysMessage("Area dump failed.\n");
                            }
                            else
                            {
                                g_Log.Event(LOGL_EVENT, "Area dump failed.\n");
                            }
							fRet = false;
						}
                        else
                        {
                            if (pSrc != this)
                            {
                                pSrc->SysMessage("Area dump successful.\n");
                            }
                            else
                            {
                                g_Log.Event(LOGL_EVENT, "Area dump successful.\n");
                            }
                        }
						break;

					case 'u': // unscripted
						pszKey++;

						switch ( tolower(*pszKey) )
						{
							case 'i': // items
							{
								pszKey++;	GETNONWHITESPACE( pszKey );
								if ( !g_Cfg.DumpUnscriptedItems( pSrc, pszKey ) )
								{
                                    if (pSrc != this)
                                    {
                                        pSrc->SysMessage("Unscripted item dump failed.\n");
                                    }
                                    else
                                    {
                                        g_Log.Event(LOGL_EVENT, "Unscripted item dump failed.\n");
                                    }
									fRet = false;
								}
                                else
                                {
                                    if (pSrc != this)
                                    {
                                        pSrc->SysMessage("Unscripted item dump successful.\n");
                                    }
                                    else
                                    {
                                        g_Log.Event(LOGL_EVENT, "Unscripted item dump successful.\n");
                                    }
                                }
								break;
							}

							default:
								goto longcommand;
						}
						break;

					default:
						goto longcommand;
				}
			} break;
		case 'e':
			{
				if ( IsSetEF(EF_Script_Profiler) )
				{
					if ( g_profiler.initstate == 0xf1 )
					{
						CScriptProfiler::CScriptProfilerFunction	*pFun;
						CScriptProfiler::CScriptProfilerTrigger		*pTrig;

						for ( pFun = g_profiler.FunctionsHead; pFun != nullptr; pFun = pFun->next )
						{
							pFun->average = pFun->max = pFun->min = pFun->total = pFun->called = 0;
						}
						for ( pTrig = g_profiler.TriggersHead; pTrig != nullptr; pTrig = pTrig->next )
						{
							pTrig->average = pTrig->max = pTrig->min = pTrig->total = pTrig->called = 0;
						}

						g_profiler.total = g_profiler.called = 0;
                        g_Log.Event(LOGL_EVENT, "Scripts profiler info cleared\n");
					}
				}
                else
                {
                    g_Log.Event(LOGL_EVENT, "Script profiler feature is not enabled on Sphere.ini.\n");
                }
                g_Log.Event(LOGL_EVENT, "Complete!\n");
			} break;
		case 'g':
			{
				if ( g_Serv.m_fResyncPause )
				{
                do_resync:
                    if (pSrc != this)
                    {
                        pSrc->SysMessage("Not allowed during resync pause. Use 'R' to restart.\n");
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "Not allowed during resync pause. Use 'R' to restart.\n");
                    }
					fRet = false;
					break;
				}
				if ( g_World.IsSaving() )
				{
	            do_saving:
                    if (pSrc != this)
                    {
                        pSrc->SysMessage("Not allowed during background worldsave. Use '#' to finish.\n");
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "Not allowed during background worldsave. Use '#' to finish.\n");
                    }
					fRet = false;
					break;
				}
				g_World.GarbageCollection();
			} break;
		case 'h':	// Hear all said.
			{
				CScript script( "HEARALL" );
				fRet = r_Verb( script, pSrc );
			} break;
		case 'i':
			{
				CScript script( "INFORMATION" );
				fRet = r_Verb( script, pSrc );
			} break;
		case 'l': // Turn the log file on or off.
			{
				CScript script( "LOG" );
				fRet = r_Verb( script, pSrc );
			} break;
		case 'p':	// Display profile information.
			{
				ProfileDump(pSrc, ( ( len > 1 ) && ( sText[1] == '#' ) ));
			} break;
		case 'r':	// resync Pause mode. Allows resync of things in files.
			{
				if ( pSrc != this ) // not from console
				{
					pSrc->SysMessage("'r' command only allowed in the console. Use 'resync' instead.\n");
				}
				else
				{
					if ( !m_fResyncPause && g_World.IsSaving() )
						goto do_saving;
					SetResyncPause(!m_fResyncPause, pSrc, true);
				}
			} break;
		case 's':
			{
				CScript script( "SECURE" );
				fRet = r_Verb( script, pSrc );
			} break;
		case 't':
			{
                if (pSrc != this)
                {
                    pSrc->SysMessagef("Current active threads: %" PRIuSIZE_T ".\n", ThreadHolder::getActiveThreads());
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "Current active threads: %" PRIuSIZE_T ".\n", ThreadHolder::getActiveThreads());
                }
				size_t iThreadCount = ThreadHolder::getActiveThreads();
				for ( size_t iThreads = 0; iThreads < iThreadCount; ++iThreads )
				{
					IThread * thrCurrent = ThreadHolder::getThreadAt(iThreads);
					if ( thrCurrent != nullptr )
						pSrc->SysMessagef("%" PRIuSIZE_T " - Id: %u, Priority: %d, Name: %s.\n", (iThreads + 1), thrCurrent->getId(),
											thrCurrent->getPriority(), thrCurrent->getName() );
				}
			} break;
		case 'u':
			TriglistPrint();
			break;
		case 'x':
			{
				if (( len > 1 ) && ( sText[1] == '#' ))	//	X# - exit with save. Such exit is not protected by secure mode
				{
					if ( g_Serv.m_fResyncPause )
						goto do_resync;

					g_World.Save(true);
					g_World.SaveStatics();
					g_Log.Event( LOGL_FATAL, "Immediate Shutdown initialized!\n");
					SetExitFlag(1);
				}
				else if ( g_Cfg.m_fSecure )
				{
                    if (pSrc != this)
                    {
                        pSrc->SysMessage("NOTE: Secure mode prevents keyboard exit!\n");
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "NOTE: Secure mode prevents keyboard exit!\n");
                    }
					fRet = false;
				}
				else
				{
					g_Log.Event( LOGL_FATAL, "Immediate Shutdown initialized!\n");
					SetExitFlag( 1 );
				}
			} break;
#ifdef _TESTEXCEPTION
		case '$':	// call stack integrity
			{
	#ifdef _EXCEPTIONS_DEBUG
				{ // test without PAUSECALLSTACK
					ADDTOCALLSTACK("CServer::TestException1");
					EXC_TRY("Test1");
					throw CSError( LOGM_DEBUG, 0, "Test Exception #1");
					}
					catch (const CSError& e)
					{
						// the following call will destroy the stack trace on linux due to
						// a call to CSFile::Close fromn CLog::EventStr.
						g_Log.Event( LOGM_DEBUG, "Caught exception\n" );
                        EXC_CATCH_EXCEPTION_SPHERE(&e);
					}
				}

				{ // test with PAUSECALLSTACK
					ADDTOCALLSTACK("CServer::TestException2");
					EXC_TRY("Test2");
					throw CSError( LOGM_DEBUG, 0, "Test Exception #2");
					}
					catch (const CSError& e)
					{
						PAUSECALLSTACK;
						// with pausecallstack, the following call won't be recorded
						g_Log.Event( LOGM_DEBUG, "Caught exception\n" );
						UNPAUSECALLSTACK;
                        EXC_CATCH_EXCEPTION_SPHERE(&e);
					}
				}
	#else
				throw CSError(LOGL_CRIT, E_FAIL, "This test requires exception debugging enabled");
	#endif
			} break;
		case '%':	// throw simple exception
			{
				throw 0;
			}
		case '^':	// throw language exception (div 0)
			{
				int i = 5;
				int j = 0;
				i /= j;
			}
		case '&':	// throw nullptr pointer exception
			{
				CChar *character = nullptr;
				character->SetName("yo!");
			}
		case '*':	// throw custom exception
			{
				throw CSError(LOGL_CRIT, (dword)E_FAIL, "Test Exception");
			}
#endif
		default:
			goto longcommand;
	}
	goto endconsole;

longcommand:
	if ((( len > 1 ) && ( sText[1] != ' ' )) || ( low == 'b' ))
	{
		lpctstr	pszText = sText;

		if ( !strnicmp(pszText, "strip", 5) || !strnicmp(pszText, "tngstrip", 8))
		{
			size_t			i = 0;
			CResourceScript	*script;
			FILE			*f, *f1;
			char			*z = Str_GetTemp();
			char			*y = Str_GetTemp();
			char			*x;
			lpctstr			dirname;

			if ( g_Cfg.m_sStripPath.IsEmpty() )
			{
                if (pSrc != this)
                {
                    pSrc->SysMessage("StripPath not defined, function aborted.\n");
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "StripPath not defined, function aborted.\n");
                }
				return false;
			}

			dirname = g_Cfg.m_sStripPath;


			if ( !strnicmp(pszText, "strip tng", 9) || !strnicmp(pszText, "tngstrip", 8))
			{
				strcpy(z, dirname);
				strcat(z, "sphere_strip_tng" SPHERE_SCRIPT);
                if (pSrc != this)
                {
                    pSrc->SysMessagef("StripFile is %s.\n", z);
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "StripFile is %s.\n", z);
                }

				f1 = fopen(z, "w");

				if ( !f1 )
				{
                    if (pSrc != this)
                    {
                        pSrc->SysMessagef("Cannot open file %s for writing.\n", z);
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "Cannot open file %s for writing.\n", z);
                    }
					return false;
				}

				while ( (script = g_Cfg.GetResourceFile(i++)) != nullptr )
				{
					strcpy(z, script->GetFilePath());
					f = fopen(z, "r");
					if ( !f )
					{
                        if (pSrc != this)
                        {
                            pSrc->SysMessagef("Cannot open file %s for reading.\n", z);
                        }
                        else
                        {
                            g_Log.Event(LOGL_EVENT, "Cannot open file %s for reading.\n", z);
                        }
						continue;
					}

					while ( !feof(f) )
					{
						z[0] = 0;
						y[0] = 0;
						fgets(y, SCRIPT_MAX_LINE_LEN, f);

						x = y;
						GETNONWHITESPACE(x);
						strcpy(z,x);

						_strlwr(z);

						if ( (( z[0] == '[' ) && strnicmp(z, "[eof]", 5) != 0) || !strnicmp(z, "defname", 7) ||
							!strnicmp(z, "name", 4) || !strnicmp(z, "type", 4) || !strnicmp(z, "id", 2) ||
							!strnicmp(z, "weight", 6) || !strnicmp(z, "value", 5) || !strnicmp(z, "dam", 3) ||
							!strnicmp(z, "armor", 5) || !strnicmp(z, "skillmake", 9) || !strnicmp(z, "on=@", 4) ||
							!strnicmp(z, "dupeitem", 8) || !strnicmp(z, "dupelist", 8) || !strnicmp(z, "p=", 2) ||
							!strnicmp(z, "can", 3) || !strnicmp(z, "tevents", 7) || !strnicmp(z, "subsection", 10) ||
							!strnicmp(z, "description", 11) || !strnicmp(z, "category", 8) || !strnicmp(z, "color", 5) ||
							!strnicmp(z, "resources", 9) )
						{
							fputs(y, f1);
						}
					}
					fclose(f);
				}
				fclose(f1);
                if (pSrc != this)
                {
                    pSrc->SysMessage("Scripts have just been stripped.\n");
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "Scripts have just been stripped.\n");
                }
				return true;
			}
			else if ( !strnicmp(pszText, "strip axis", 10) || !strnicmp(pszText, "strip", 5) )
			{
				strcpy(z, dirname);
				strcat(z, "sphere_strip_axis" SPHERE_SCRIPT);
                if (pSrc != this)
                {
                    pSrc->SysMessagef("StripFile is %s.\n", z);
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "StripFile is %s.\n", z);
                }

				f1 = fopen(z, "w");

				if ( !f1 )
				{
                    if (pSrc != this)
                    {
                        pSrc->SysMessagef("Cannot open file %s for writing.\n", z);
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "Cannot open file %s for writing.\n", z);
                    }
					return false;
				}

				while ( (script = g_Cfg.GetResourceFile(i++)) != nullptr )
				{
					strcpy(z, script->GetFilePath());
					f = fopen(z, "r");
					if ( !f )
					{
                        if (pSrc != this)
                        {
                            pSrc->SysMessagef("Cannot open file %s for reading.\n", z);
                        }
                        else
                        {
                            g_Log.Event(LOGL_EVENT, "Cannot open file %s for reading.\n", z);
                        }
						continue;
					}

					while ( !feof(f) )
					{
						z[0] = 0;
						y[0] = 0;
						fgets(y, SCRIPT_MAX_LINE_LEN, f);

						x = y;
						GETNONWHITESPACE(x);
						strcpy(z,x);

						_strlwr(z);

						if ( (( z[0] == '[' ) && strnicmp(z, "[eof]", 5) != 0) || !strnicmp(z, "defname", 7) ||
							!strnicmp(z, "name", 4) || !strnicmp(z, "type", 4) || !strnicmp(z, "id", 2) ||
							!strnicmp(z, "weight", 6) || !strnicmp(z, "value", 5) || !strnicmp(z, "dam", 3) ||
							!strnicmp(z, "armor", 5) || !strnicmp(z, "skillmake", 9) || !strnicmp(z, "on=@", 4) ||
							!strnicmp(z, "dupeitem", 8) || !strnicmp(z, "dupelist", 8) || !strnicmp(z, "can", 3) ||
							!strnicmp(z, "tevents", 7) || !strnicmp(z, "subsection", 10) || !strnicmp(z, "description", 11) ||
							!strnicmp(z, "category", 8) || !strnicmp(z, "p=", 2) || !strnicmp(z, "resources", 9) ||
							!strnicmp(z, "group", 5) || !strnicmp(z, "rect=", 5) )
						{
							fputs(y, f1);
						}
					}
					fclose(f);
				}
				fclose(f1);
                if (pSrc != this)
                {
                    pSrc->SysMessagef("Scripts have just been stripped.\n");
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "Scripts have just been stripped.\n");
                }
				return true;
			}
		}

		if ( g_Cfg.IsConsoleCmd(low) )
			pszText++;

		CScript	script(pszText);
		if ( !g_Cfg.CanUsePrivVerb(this, pszText, pSrc) )
		{
            if (pSrc != this)
            {
                pSrc->SysMessagef("not privileged for command '%s'\n", pszText);
            }
            else
            {
                g_Log.Event(LOGL_EVENT, "not privileged for command '%s'\n", pszText);
            }
			fRet = false;
		}
		else if ( !r_Verb(script, pSrc) )
		{
            if (pSrc != this)
            {
                pSrc->SysMessagef("unknown command '%s'\n", pszText);
            }
            else
            {
                g_Log.Event(LOGL_EVENT, "unknown command '%s'\n", pszText);
            }
			fRet = false;
		}
	}
	else
	{
        if (pSrc != this)
        {
            pSrc->SysMessagef("unknown command '%s'\n", static_cast<lpctstr>(sText));
        }
        else
        {
            g_Log.Event(LOGL_EVENT, "unknown command '%s'\n", static_cast<lpctstr>(sText));
        }
		fRet = false;
	}

endconsole:
	sText.Empty();
	return( fRet );
}

//************************************************

PLEVEL_TYPE CServer::GetPrivLevel() const
{
	return PLEVEL_Owner;
}

void CServer::ProfileDump( CTextConsole * pSrc, bool bDump )
{
	ADDTOCALLSTACK("CServer::ProfileDump");
	if ( !pSrc )
		return;

	CSFileText * ftDump = nullptr;

	if ( bDump )
	{
		ftDump = new CSFileText();
		if ( ! ftDump->Open("profiler_dump.txt", OF_CREATE|OF_TEXT) )
		{
			delete ftDump;
			ftDump = nullptr;
		}
	}

    if (pSrc != this)
    {
        pSrc->SysMessagef("Profiles %s: (%d sec total)\n", CurrentProfileData.IsActive() ? "ON" : "OFF", CurrentProfileData.GetActiveWindow());
    }
    else
    {
        g_Log.Event(LOGL_EVENT, "Profiles %s: (%d sec total)\n", CurrentProfileData.IsActive() ? "ON" : "OFF", CurrentProfileData.GetActiveWindow());
    }
    if (ftDump != nullptr)
    {
        ftDump->Printf("Profiles %s: (%d sec total)\n", CurrentProfileData.IsActive() ? "ON" : "OFF", CurrentProfileData.GetActiveWindow());
    }

	size_t iThreadCount = ThreadHolder::getActiveThreads();
	for ( size_t iThreads = 0; iThreads < iThreadCount; ++iThreads)
	{
		IThread* thrCurrent = ThreadHolder::getThreadAt(iThreads);
		if (thrCurrent == nullptr)
			continue;

		const ProfileData& profile = static_cast<AbstractSphereThread*>(thrCurrent)->m_profile;
		if (profile.IsEnabled() == false)
			continue;

        if (pSrc != this)
        {
            pSrc->SysMessagef("Thread %u, Name=%s\n", thrCurrent->getId(), thrCurrent->getName());
        }
        else
        {
            g_Log.Event(LOGL_EVENT, "Thread %u, Name=%s\n", thrCurrent->getId(), thrCurrent->getName());
        }
		if (ftDump != nullptr)
			ftDump->Printf("Thread %u, Name=%s\n", thrCurrent->getId(), thrCurrent->getName());

		for (int i = 0; i < PROFILE_QTY; i++)
		{
			if (profile.IsEnabled(static_cast<PROFILE_TYPE>(i)) == false)
				continue;

            if (pSrc != this)
            {
                pSrc->SysMessagef("%-10s = %s\n", profile.GetName(static_cast<PROFILE_TYPE>(i)), profile.GetDescription(static_cast<PROFILE_TYPE>(i)));
            }
            else
            {
                g_Log.Event(LOGL_EVENT, "%-10s = %s\n", profile.GetName(static_cast<PROFILE_TYPE>(i)), profile.GetDescription(static_cast<PROFILE_TYPE>(i)));
            }
			if (ftDump != nullptr)
				ftDump->Printf( "%-10s = %s\n", profile.GetName(static_cast<PROFILE_TYPE>(i)), profile.GetDescription(static_cast<PROFILE_TYPE>(i)) );
		}
	}

	if ( IsSetEF(EF_Script_Profiler) )
	{
        if (g_profiler.initstate != 0xf1)
        {
            if (pSrc != this)
            {
                pSrc->SysMessage("Scripts profiler is not initialized\n");
            }
            else
            {
                g_Log.Event(LOGL_EVENT, "Scripts profiler is not initialized\n");
            }
        }
        else if (!g_profiler.called)
        {
            if (pSrc != this)
            {
                pSrc->SysMessage("Script profiler is not yet informational\n");
            }
            else
            {
                g_Log.Event(LOGL_EVENT, "Script profiler is not yet informational\n");
            }
        }
		else
		{
			llong average = g_profiler.total / g_profiler.called;
			CScriptProfiler::CScriptProfilerFunction * pFun;
			CScriptProfiler::CScriptProfilerTrigger * pTrig;
			llong divby = llTimeProfileFrequency / 1000;

            char tmpstring[255];
            sprintf(tmpstring, "Scripts: called %u times and took %i.%04i msec (%i.%04i msec average). Reporting with highest average.\n",
                g_profiler.called,
                (int)(g_profiler.total / divby),
                (int)(((g_profiler.total * 10000) / (divby)) % 10000),
                (int)(average / divby),
                (int)(((average * 10000) / (divby)) % 10000));
            if (pSrc != this)
            {
                pSrc->SysMessage(tmpstring);
            }
            else
            {
                g_Log.Event(LOGL_EVENT, tmpstring);
            }
            if (ftDump != nullptr)
            {
                ftDump->Printf(tmpstring);
            }

			for ( pFun = g_profiler.FunctionsHead; pFun != nullptr; pFun = pFun->next )
			{
				if ( pFun->average > average )
				{
                    sprintf(tmpstring, "FUNCTION '%s' called %u times, took %i.%04i msec average (%i.%04i min, %i.%04i max), total: %i.%04i msec\n",
                        pFun->name,
                        pFun->called,
                        (int)(pFun->average / divby),
                        (int)(((pFun->average * 10000) / (divby)) % 10000),
                        (int)(pFun->min / divby),
                        (int)(((pFun->min * 10000) / (divby)) % 10000),
                        (int)(pFun->max / divby),
                        (int)(((pFun->max * 10000) / (divby)) % 10000),
                        (int)(pFun->total / divby),
                        (int)(((pFun->total * 10000) / (divby)) % 10000));

                    if (pSrc != this)
                    {
                        pSrc->SysMessage(tmpstring);
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, tmpstring);
                    }
                    if (ftDump != nullptr)
                    {
                        ftDump->Printf(tmpstring);
                    }
				}
			}
			for ( pTrig = g_profiler.TriggersHead; pTrig != nullptr; pTrig = pTrig->next )
			{
				if ( pTrig->average > average )
				{
					sprintf(tmpstring, "TRIGGER '%s' called %u times, took %i.%04i msec average (%i.%04i min, %i.%04i max), total: %i.%04i msec\n",
						pTrig->name,
						pTrig->called,
						(int)(pTrig->average / divby),
						(int)(((pTrig->average * 10000) / (divby)) % 10000),
						(int)(pTrig->min / divby),
						(int)(((pTrig->min * 10000) / (divby)) % 10000),
						(int)(pTrig->max / divby),
						(int)(((pTrig->max * 10000) / (divby)) % 10000),
						(int)(pTrig->total / divby),
						(int)(((pTrig->total * 10000) / (divby)) % 10000)
					);
                    if (pSrc != this)
                    {
                        pSrc->SysMessage(tmpstring);
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, tmpstring);
                    }
                    if (ftDump != nullptr)
                    {
                        ftDump->Printf(tmpstring);
                    }
				}
			}

            if (pSrc != this)
            {
                pSrc->SysMessage("Report complete!\n");
            }
            else
            {
                g_Log.Event(LOGL_EVENT, "Report complete!\n");
            }
		}
	}
    else
    {
        if (pSrc != this)
        {
            pSrc->SysMessage("Script profiler feature is not enabled on Sphere.ini.\n");
        }
        else
        {
            g_Log.Event(LOGL_EVENT, "Script profiler feature is not enabled on Sphere.ini.\n");
        }
    }

	if ( ftDump != nullptr )
	{
		ftDump->Close();
		delete ftDump;
	}
}

// ---------------------------------------------------------------------

bool CServer::r_GetRef( lpctstr & pszKey, CScriptObj * & pRef )
{
	ADDTOCALLSTACK("CServer::r_GetRef");
	if ( IsDigit( pszKey[0] ))
	{
		size_t i = 1;
		while ( IsDigit( pszKey[i] ))
			i++;

		if ( pszKey[i] == '.' )
		{
			size_t index = ATOI( pszKey );	// must use this to stop at .
			pRef = g_Cfg.Server_GetDef(index);
			pszKey += i + 1;
			return true;
		}
	}
	if ( g_Cfg.r_GetRef( pszKey, pRef ))
	{
		return true;
	}
	if ( g_World.r_GetRef( pszKey, pRef ))
	{
		return true;
	}
	return( CScriptObj::r_GetRef( pszKey, pRef ));
}

bool CServer::r_LoadVal( CScript &s )
{
	ADDTOCALLSTACK("CServer::r_LoadVal");

	if ( g_Cfg.r_LoadVal(s) )
		return true;
	if ( g_World.r_LoadVal(s) )
		return true;
	return CServerDef::r_LoadVal(s);
}

bool CServer::r_WriteVal( lpctstr pszKey, CSString & sVal, CTextConsole * pSrc )
{
	ADDTOCALLSTACK("CServer::r_WriteVal");
	if ( !strnicmp(pszKey, "ACCOUNT.", 8) )
	{
		pszKey += 8;
		CAccount * pAccount = nullptr;

		// extract account name/index to a temporary buffer
		tchar * pszTemp = Str_GetTemp();
		tchar * pszTempStart = pszTemp;

		strcpy(pszTemp, pszKey);
		tchar * split = strchr(pszTemp, '.');
		if ( split != nullptr )
			*split = '\0';

		// adjust pszKey to point to end of account name/index
		pszKey += strlen(pszTemp);

		//	try to fetch using indexes
		if (( *pszTemp >= '0' ) && ( *pszTemp <= '9' ))
		{
			size_t num = Exp_GetVal(pszTemp);
			if (*pszTemp == '\0' && num < g_Accounts.Account_GetCount())
				pAccount = g_Accounts.Account_Get(num);
		}

		//	indexes failed, try to fetch using names
		if ( pAccount == nullptr )
		{
			pszTemp = pszTempStart;
			pAccount = g_Accounts.Account_Find(pszTemp);
		}

		if ( !*pszKey) // we're just checking if the account exists
		{
			sVal.FormatVal( (pAccount? 1 : 0) );
			return true;
		}
		else if ( pAccount ) // we're retrieving a property from the account
		{
			SKIP_SEPARATORS(pszKey);
			return pAccount->r_WriteVal(pszKey, sVal, pSrc);
		}
		// we're trying to retrieve a property from an invalid account
		return false;
	}

	// Just do stats values for now.
	if ( g_Cfg.r_WriteVal(pszKey, sVal, pSrc) )
		return true;
	if ( g_World.r_WriteVal(pszKey, sVal, pSrc) )
		return true;
	return CServerDef::r_WriteVal(pszKey, sVal, pSrc);
}

enum SV_TYPE
{
	SV_ACCOUNT,
	SV_ACCOUNTS, //read only
	SV_ALLCLIENTS,
	SV_B,
	SV_BLOCKIP,
	SV_CHARS, //read only
	SV_CLEARLISTS,
	SV_CONSOLE,
#ifdef _TEST_EXCEPTION
	SV_CRASH,
#endif
	SV_EXPORT,
	SV_GARBAGE,
	SV_HEARALL,
	SV_IMPORT,
	SV_INFORMATION,
	SV_ITEMS, //read only
	SV_LOAD,
	SV_LOG,
	SV_PRINTLISTS,
	SV_RESPAWN,
	SV_RESTOCK,
	SV_RESTORE,
	SV_RESYNC,
	SV_SAVE,
	SV_SAVECOUNT, //read only
	SV_SAVESTATICS,
	SV_SECURE,
	SV_SHRINKMEM,
	SV_SHUTDOWN,
	SV_TIME, // read only
	SV_UNBLOCKIP,
	SV_VARLIST,
	SV_QTY
};

lpctstr const CServer::sm_szVerbKeys[SV_QTY+1] =
{
	"ACCOUNT",
	"ACCOUNTS", // read only
	"ALLCLIENTS",
	"B",
	"BLOCKIP",
	"CHARS", // read only
	"CLEARLISTS",
	"CONSOLE",
#ifdef _TEST_EXCEPTION
	"CRASH",
#endif
	"EXPORT",
	"GARBAGE",
	"HEARALL",
	"IMPORT",
	"INFORMATION",
	"ITEMS", // read only
	"LOAD",
	"LOG",
	"PRINTLISTS",
	"RESPAWN",
	"RESTOCK",
	"RESTORE",
	"RESYNC",
	"SAVE",
	"SAVECOUNT", // read only
	"SAVESTATICS",
	"SECURE",
	"SHRINKMEM",
	"SHUTDOWN",
	"TIME", // read only
	"UNBLOCKIP",
	"VARLIST",
	nullptr
};

bool CServer::r_Verb( CScript &s, CTextConsole * pSrc )
{
	ADDTOCALLSTACK("CServer::r_Verb");
	if ( !pSrc )
		return false;

	EXC_TRY("Verb");
	lpctstr pszKey = s.GetKey();
	tchar *pszMsg = nullptr;

	int index = FindTableSorted( s.GetKey(), sm_szVerbKeys, CountOf( sm_szVerbKeys )-1 );

	if ( index < 0 )
	{
		CSString sVal;
		CScriptTriggerArgs Args( s.GetArgRaw() );
		if ( r_Call( pszKey, pSrc, &Args, &sVal ) )
			return true;

		if ( !strnicmp(pszKey, "ACCOUNT.", 8) )
		{
			index = SV_ACCOUNT;
			pszKey += 8;

			CAccount * pAccount = nullptr;
			char *pszTemp = Str_GetTemp();

			strcpy(pszTemp, pszKey);
			char *split = strchr(pszTemp, '.');
			if ( split )
			{
				*split = 0;
				pAccount = g_Accounts.Account_Find(pszTemp);
				pszKey += strlen(pszTemp);
			}

			SKIP_SEPARATORS(pszKey);
			if ( pAccount && *pszKey )
			{
				CScript script(pszKey, s.GetArgStr());
				script.m_iResourceFileIndex = s.m_iResourceFileIndex;	// Index in g_Cfg.m_ResourceFiles of the CResourceScript (script file) where the CScript originated
				script.m_iLineNum = s.m_iLineNum;						// Line in the script file where Key/Arg were read
				return pAccount->r_LoadVal(script);
			}
			return false;
		}

		if ( !strnicmp(pszKey, "CLEARVARS", 9) )
		{
			pszKey = s.GetArgStr();
			SKIP_SEPARATORS(pszKey);
			g_Exp.m_VarGlobals.ClearKeys(pszKey);
			return true;
		}
	}

	switch (index)
	{
		case SV_ACCOUNTS:
		case SV_CHARS:
		case SV_ITEMS:
		case SV_SAVECOUNT:
		case SV_TIME:
			return false;
		case SV_ACCOUNT: // "ACCOUNT"
			return g_Accounts.Account_OnCmd(s.GetArgRaw(), pSrc);

		case SV_ALLCLIENTS:	// "ALLCLIENTS"
			{
				// Send a verb to all clients
				ClientIterator it;
				for (CClient* pClient = it.next(); pClient != nullptr; pClient = it.next())
				{
					if ( pClient->GetChar() == nullptr )
						continue;
					CScript script(s.GetArgStr());
					script.m_iResourceFileIndex = s.m_iResourceFileIndex;	// Index in g_Cfg.m_ResourceFiles of the CResourceScript (script file) where the CScript originated
					script.m_iLineNum = s.m_iLineNum;						// Line in the script file where Key/Arg were read
					pClient->GetChar()->r_Verb( script, pSrc );
				}
			}
			break;

		case SV_B: // "B"
			g_World.Broadcast( s.GetArgStr());
			break;

		case SV_BLOCKIP:
			if ( pSrc->GetPrivLevel() >= PLEVEL_Admin )
			{
				int iTimeDecay(-1);

				tchar* ppArgs[2];
				if (Str_ParseCmds(s.GetArgRaw(), ppArgs, CountOf(ppArgs), ", ") == false)
					return false;

				if (ppArgs[1])
					iTimeDecay = Exp_GetVal(ppArgs[1]);

#ifndef _MTNETWORK
				HistoryIP& history = g_NetworkIn.getIPHistoryManager().getHistoryForIP(ppArgs[0]);
#else
				HistoryIP& history = g_NetworkManager.getIPHistoryManager().getHistoryForIP(ppArgs[0]);
#endif

                if (iTimeDecay >= 0)
                {
                    if (pSrc != this)
                    {
                        pSrc->SysMessagef("IP blocked for %d seconds\n", iTimeDecay);
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "IP blocked for %d seconds\n", iTimeDecay);
                    }
                }
                else
                {
                    if (pSrc != this)
                    {
                        pSrc->SysMessagef("IP%s blocked\n", history.m_blocked ? " already" : "");
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "IP%s blocked\n", history.m_blocked ? " already" : "");
                    }
                }

				history.setBlocked(true, iTimeDecay);
			}
			break;

		case SV_CONSOLE:
			{
				CSString z = s.GetArgRaw();
				OnConsoleCmd(z, pSrc);
			}
			break;

#ifdef _TEST_EXCEPTION
		case SV_CRASH:
			{
				g_Log.Event(0, "Testing exceptions.\n");
				pSrc = nullptr;
				pSrc->GetChar();
			} break;
#endif

		case SV_EXPORT: // "EXPORT" name [chars] [area distance]
			if ( pSrc->GetPrivLevel() < PLEVEL_Admin )
				return false;
			if ( s.HasArgs())
			{
				tchar * Arg_ppCmd[5];
				int Arg_Qty = Str_ParseCmds( s.GetArgRaw(), Arg_ppCmd, CountOf( Arg_ppCmd ) );
				if ( Arg_Qty <= 0 )
					break;
				// IMPFLAGS_ITEMS
				if ( ! g_World.Export( Arg_ppCmd[0], pSrc->GetChar(),
					(Arg_Qty >= 2) ? (word)ATOI(Arg_ppCmd[1]) : (word)IMPFLAGS_ITEMS,
					(Arg_Qty >= 3)? ATOI(Arg_ppCmd[2]) : INT16_MAX ))
				{
                    if (pSrc != this)
                    {
                        pSrc->SysMessage("Export failed\n");
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "Export failed\n");
                    }
				}
			}
			else
			{
                if (pSrc != this)
                {
                    pSrc->SysMessage("EXPORT name [flags] [area distance]");
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "EXPORT name [flags] [area distance]");
                }
			}
			break;
		case SV_GARBAGE:
			if ( g_Serv.m_fResyncPause || g_World.IsSaving() )
			{
                if (pSrc != this)
                {
                    pSrc->SysMessage("Not allowed during world save and/or resync pause");
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "Not allowed during world save and/or resync pause");
                }
				break;
			}
			else
			{
				g_World.GarbageCollection();
			}
			break;

		case SV_HEARALL:	// "HEARALL" = Hear all said.
			{
				pszMsg = Str_GetTemp();
				g_Log.SetLogMask( s.GetArgFlag( g_Log.GetLogMask(), LOGM_PLAYER_SPEAK ));
				sprintf(pszMsg, "Hear All %s.\n", g_Log.IsLoggedMask(LOGM_PLAYER_SPEAK) ? "Enabled" : "Disabled" );
			}
			break;

		case SV_INFORMATION:
            {
                if (pSrc != this)
                {
                    pSrc->SysMessage(GetStatusString(0x22));
                    pSrc->SysMessage(GetStatusString(0x24));
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "%s", GetStatusString(0x22));
                    g_Log.Event(LOGL_EVENT, "%s", GetStatusString(0x24));
                }
            }
			break;

		case SV_IMPORT: // "IMPORT" name [flags] [area distance]
			if ( pSrc->GetPrivLevel() < PLEVEL_Admin )
				return false;
			if (s.HasArgs())
			{
				tchar * Arg_ppCmd[5];
				int Arg_Qty = Str_ParseCmds( s.GetArgRaw(), Arg_ppCmd, CountOf( Arg_ppCmd ));
				if ( Arg_Qty <= 0 )
				{
					break;
				}
				// IMPFLAGS_ITEMS
                if (!g_World.Import(Arg_ppCmd[0], pSrc->GetChar(),
                    (Arg_Qty >= 2) ? (word)(ATOI(Arg_ppCmd[1])) : (word)IMPFLAGS_BOTH,
                    (Arg_Qty >= 3) ? ATOI(Arg_ppCmd[2]) : INT16_MAX))
                {
                    if (pSrc != this)
                    {
                        pSrc->SysMessage("Import failed\n");
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "Import failed\n");
                    }
                }
			}
			else
			{
                if (pSrc != this)
                {
                    pSrc->SysMessage("IMPORT name [flags] [area distance]");
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "IMPORT name [flags] [area distance]");
                }
			}
			break;
		case SV_LOAD:
			// Load a resource file.
			if ( g_Cfg.LoadResourcesAdd( s.GetArgStr()) == nullptr )
				return false;
			return true;

		case SV_LOG:
			{
				lpctstr	pszArgs = s.GetArgStr();
				int	 mask = LOGL_EVENT;
				if ( pszArgs && ( *pszArgs == '@' ))
				{
					++pszArgs;
					if ( *pszArgs != '@' )
						mask |= LOGM_NOCONTEXT;
				}
				g_Log.Event(mask, "%s\n", pszArgs);
			}
			break;

		case SV_RESPAWN:
			g_World.RespawnDeadNPCs();
			break;

		case SV_RESTOCK:
			// set restock time of all vendors in World.
			// set the respawn time of all spawns in World.
			g_World.Restock();
			break;

		case SV_RESTORE:	// "RESTORE" backupfile.SCP Account CharName
			if ( pSrc->GetPrivLevel() < PLEVEL_Admin )
				return false;
			if (s.HasArgs())
			{
				tchar * Arg_ppCmd[4];
				size_t Arg_Qty = Str_ParseCmds( s.GetArgRaw(), Arg_ppCmd, CountOf( Arg_ppCmd ));
				if ( Arg_Qty <= 0 )
				{
					break;
				}
				if ( ! g_World.Import( Arg_ppCmd[0], pSrc->GetChar(),
					IMPFLAGS_BOTH|IMPFLAGS_ACCOUNT, INT16_MAX,
					Arg_ppCmd[1], Arg_ppCmd[2] ))
				{
                    if (pSrc != this)
                    {
                        pSrc->SysMessage("Restore failed\n");
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "Restore failed\n");
                    }
				}
				else
				{
                    if (pSrc != this)
                    {
                        pSrc->SysMessage("Restore success\n");
                    }
                    else
                    {
                        g_Log.Event(LOGL_EVENT, "Restore success\n");
                    }
				}
			}
			break;
		case SV_RESYNC:
			{
				// if a resync occurs whilst a script is executing then bad thing
				// will happen. Flag that a resync has been requested and it will
				// take place during a server tick
				m_fResyncRequested = pSrc;
				break;
			}
		case SV_SAVE: // "SAVE" x
			g_World.Save(s.GetArgVal() != 0);
			break;
		case SV_SAVESTATICS:
			g_World.SaveStatics();
			break;
		case SV_SECURE: // "SECURE"
			pszMsg = Str_GetTemp();
			g_Cfg.m_fSecure = s.GetArgFlag( g_Cfg.m_fSecure, true ) != 0;
			SetSignals();
			sprintf(pszMsg, "Secure mode %s.\n", g_Cfg.m_fSecure ? "re-enabled" : "disabled" );
			break;
		case SV_SHRINKMEM:
			{
#ifdef _WIN32
				if ( Sphere_GetOSInfo()->dwPlatformId != 2 )
				{
					g_Log.EventError("Command not avaible on Windows 95/98/ME.\n");
					return false;
				}

				if ( !SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)) )
				{
					g_Log.EventError("Error during process shrink.\n");
					return false;
				}

                if (pSrc != this)
                {
                    pSrc->SysMessage("Memory shrinked succesfully.\n");
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "Memory shrinked succesfully.\n");
                }
#else
				g_Log.EventError( "Command not avaible on *NIX os.\n" );
				return false;
#endif
			} break;

		case SV_SHUTDOWN: // "SHUTDOWN"
			Shutdown( s.HasArgs() ? s.GetArgVal() : 15 );
			break;

		case SV_UNBLOCKIP:	// "UNBLOCKIP"
			if (pSrc->GetPrivLevel() >= PLEVEL_Admin)
			{
#ifndef _MTNETWORK
				HistoryIP& history = g_NetworkIn.getIPHistoryManager().getHistoryForIP(s.GetArgRaw());
#else
				HistoryIP& history = g_NetworkManager.getIPHistoryManager().getHistoryForIP(s.GetArgRaw());
#endif
                if (pSrc != this)
                {
                    pSrc->SysMessagef("IP%s unblocked\n", history.m_blocked ? "" : " already");
                }
                else
                {
                    g_Log.Event(LOGL_EVENT, "IP%s unblocked\n", history.m_blocked ? "" : " already");
                }
				history.setBlocked(false);
			}
			break;

		case SV_VARLIST:
			if ( ! strcmpi( s.GetArgStr(), "log" ) )
				pSrc = &g_Serv;
			g_Exp.m_VarGlobals.DumpKeys(pSrc, "VAR.");
			break;
		case SV_PRINTLISTS:
			if ( ! strcmpi( s.GetArgStr(), "log" ) )
				pSrc = &g_Serv;
			g_Exp.m_ListGlobals.DumpKeys(pSrc, "LIST.");
			break;
		case SV_CLEARLISTS:
			g_Exp.m_ListGlobals.ClearKeys( s.GetArgStr()) ;
			break;
		default:
			return CScriptObj::r_Verb(s, pSrc);
	}

    if (pszMsg && *pszMsg)
    {
        if (pSrc != this)
        {
            pSrc->SysMessage(pszMsg);
        }
        else
        {
            g_Log.Event(LOGL_EVENT, pszMsg);
        }
    }

	return true;
	EXC_CATCH;

	EXC_DEBUG_START;
	EXC_ADD_SCRIPTSRC;
	g_Log.EventDebug("source '%s' char '%s' uid '0%x'\n", (pSrc && pSrc->GetName()) ? pSrc->GetName() : "",
		(pSrc && pSrc->GetChar()) ? pSrc->GetChar()->GetName() : "",
		(pSrc && pSrc->GetChar()) ? (dword)pSrc->GetChar()->GetUID() : 0 );
	EXC_DEBUG_END;
	return false;
}

//*********************************************************

extern void defragSphere(char *);

bool CServer::CommandLine( int argc, tchar * argv[] )
{
	// Console Command line.
	// This runs after script file enum but before loading the world file.
	// RETURN:
	//  true = keep running after this.

	for ( int argn = 1; argn < argc; argn++ )
	{
		tchar * pArg = argv[argn];
		if ( ! _IS_SWITCH(pArg[0]))
			continue;

		pArg++;

		switch ( toupper(pArg[0]) )
		{
			case '?':
				PrintStr( SPHERE_TITLE " \n"
					"Command Line Switches:\n"
#ifdef _WIN32
					"-cClassName Setup custom window class name for sphere (default: " SPHERE_TITLE "Svr)\n"
#else
					"-c do not use colored console output (default: on)\n"
#endif
					"-D Dump global variable DEFNAMEs to defs.txt\n"
#if defined(_WIN32) && !defined(_DEBUG)
					"-E Enable the CrashDumper\n"
#endif
					"-Gpath/to/saves/ Defrags sphere saves\n"
#ifdef _WIN32
					"-k install/remove Installs or removes NT Service\n"
#endif
					"-Nstring Set the sphere name.\n"
					"-P# Set the port number.\n"
					"-Ofilename Output console to this file name\n"
					"-Q Quit when finished.\n"
					);
				return false;
#ifdef _WIN32
			case 'C':
			case 'K':
				//	these are parsed in other places - nt service, nt window part, etc
				continue;
#else
			case 'C':
				g_UnixTerminal.setColorEnabled(false);
				continue;
#endif
			case 'P':
				m_ip.SetPort((word)(ATOI(pArg + 1)));
				continue;
			case 'N':
				// Set the system name.
				SetName(pArg+1);
				continue;
			case 'D':
				// dump all the defines to a file.
				{
					CSFileText File;
					if ( ! File.Open( "defs.txt", OF_WRITE|OF_TEXT ))
						return false;

					for ( size_t i = 0; i < g_Exp.m_VarDefs.GetCount(); i++ )
					{
						if ( ( i % 0x1ff ) == 0 )
							PrintPercent( (ssize_t)i, (ssize_t)g_Exp.m_VarDefs.GetCount() );

						CVarDefCont * pCont = g_Exp.m_VarDefs.GetAt(i);
						if ( pCont != nullptr )
						{
							File.Printf( "%s=%s\n", pCont->GetKey(), pCont->GetValStr());
						}
					}
				}
				continue;
#if defined(_WIN32) && !defined(_DEBUG) && !defined(_NO_CRASHDUMP)
			case 'E':
				CrashDump::Enable();
				if (CrashDump::IsEnabled())
					PrintStr("Crash dump enabled\n");
				else
					PrintStr("Crash dump NOT enabled.\n");
				continue;
#endif
			case 'G':
				defragSphere(pArg + 1);
				continue;
			case 'O':
				if ( g_Log.Open(pArg+1, OF_SHARE_DENY_WRITE|OF_READWRITE|OF_TEXT) )
					g_Log.m_fLockOpen = true;
				continue;
			case 'Q':
				return false;
			default:
				g_Log.Event(LOGM_INIT|LOGL_CRIT, "Don't recognize command line data '%s'\n", static_cast<lpctstr>(argv[argn]));
				break;
		}
	}

	return true;
}

void CServer::SetResyncPause(bool fPause, CTextConsole * pSrc, bool bMessage)
{
	ADDTOCALLSTACK("CServer::SetResyncPause");
	if ( fPause )
	{
		m_fResyncPause = true;
        g_Log.Event(LOGL_EVENT, "%s\n", g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_START));

		if ( bMessage )
			g_World.Broadcast(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_START));
		else if ( pSrc && pSrc->GetChar() )
			pSrc->SysMessage(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_START));

		g_Cfg.Unload(true);
		SetServerMode(SERVMODE_ResyncPause);
	}
	else
	{
        g_Log.Event(LOGL_EVENT, "%s\n", g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_RESTART));
		SetServerMode(SERVMODE_ResyncLoad);

		if ( !g_Cfg.Load(true) )
		{
            g_Log.EventError("%s\n", g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_FAILED));
			if ( bMessage )
				g_World.Broadcast(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_FAILED));
			else if ( pSrc && pSrc->GetChar() )
				pSrc->SysMessage(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_FAILED));
		}
		else
		{
            g_Log.Event(LOGL_EVENT, "%s\n", g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_SUCCESS));
			if ( bMessage )
				g_World.Broadcast(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_SUCCESS));
			else if ( pSrc && pSrc->GetChar() )
				pSrc->SysMessage(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_SUCCESS));
		}

		m_fResyncPause = false;
		SetServerMode(SERVMODE_Run);
	}
}

//*********************************************************

bool CServer::SocketsInit( CSocket & socket )
{
	ADDTOCALLSTACK("CServer::SocketsInit");
	// Initialize socket
	if ( !socket.Create() )
	{
		g_Log.Event(LOGL_FATAL|LOGM_INIT, "Unable to create socket!\n");
		return false;
	}

	linger lval;
	lval.l_onoff = 0;
	lval.l_linger = 10;
	socket.SetSockOpt(SO_LINGER, reinterpret_cast<const char *>(&lval), sizeof(lval));
	socket.SetNonBlocking();

#ifndef _WIN32
	int onNotOff = 1;
	if(socket.SetSockOpt(SO_REUSEADDR, (char *)&onNotOff , sizeof(onNotOff )) == -1) {
		g_Log.Event(LOGL_FATAL|LOGM_INIT, "Unable to set SO_REUSEADDR!\n");
	}
#endif
	// Bind to just one specific port if they say so.
	CSocketAddress SockAddr = m_ip;
	// if ( fGod )
	//	SockAddr.SetPort(( g_Cfg.m_iUseGodPort > 1 ) ? g_Cfg.m_iUseGodPort : m_ip.GetPort()+1000);

	int iRet = socket.Bind(SockAddr);
	if ( iRet < 0 )			// Probably already a server running.
	{
		g_Log.Event(LOGL_FATAL|LOGM_INIT, "Unable to bind listen socket %s port %d (error code: %i)\n", SockAddr.GetAddrStr(), SockAddr.GetPort(), iRet);
		return false;
	}
	socket.Listen();

#ifdef _LIBEV
#ifdef LIBEV_REGISTERMAIN
	if ( g_Cfg.m_fUseAsyncNetwork != 0 )
		g_NetworkEvent.registerMainsocket();
#endif
#endif

	return true;
}

bool CServer::SocketsInit() // Initialize sockets
{
	ADDTOCALLSTACK("CServer::SocketsInit");
	if ( !SocketsInit(m_SocketMain) )
		return false;

	// What are we listing our port as to the world.
	// Tell the admin what we know.

	tchar szName[ _MAX_PATH ];
	struct hostent * pHost = nullptr;

	int iRet = gethostname(szName, sizeof(szName));
	if ( iRet )
		strcpy(szName, m_ip.GetAddrStr());
	else
	{
		pHost = gethostbyname(szName);
		if ( pHost && pHost->h_addr && pHost->h_name && pHost->h_name[0] )
			strcpy(szName, pHost->h_name);
	}

	g_Log.Event( LOGM_INIT, "Server started on hostname '%s'\n", szName);
	if ( !iRet && pHost && pHost->h_addr )
	{
		for ( size_t i = 0; pHost->h_addr_list[i] != nullptr; i++ )
		{
			CSocketAddressIP ip;
			ip.SetAddrIP(*((dword*)(pHost->h_addr_list[i]))); // 0.1.2.3
			if ( !m_ip.IsLocalAddr() && !m_ip.IsSameIP(ip) )
				continue;
			g_Log.Event(LOGM_INIT, "Monitoring IP %s:%d\n", ip.GetAddrStr(), m_ip.GetPort());
		}
	}
	return true;
}

void CServer::SocketsClose()
{
	ADDTOCALLSTACK("CServer::SocketsClose");
#ifdef _LIBEV
#ifdef LIBEV_REGISTERMAIN
	if ( g_Cfg.m_fUseAsyncNetwork != 0 )
		g_NetworkEvent.unregisterMainsocket();
#endif
#endif
	m_SocketMain.Close();
}

void CServer::OnTick()
{
	ADDTOCALLSTACK("CServer::OnTick");
	EXC_TRY("Tick");

#ifndef _WIN32
	if (g_UnixTerminal.isReady())
	{
		tchar c = g_UnixTerminal.read();
		if ( OnConsoleKey(m_sConsoleText, c, false) == 2 )
			m_fConsoleTextReadyFlag = true;
	}
#endif

	EXC_SET_BLOCK("ConsoleInput");
	if ( m_fConsoleTextReadyFlag )
	{
		EXC_SET_BLOCK("console input");
		CSString sText = m_sConsoleText;	// make a copy.
		m_sConsoleText.Empty();				// done using this.
		m_fConsoleTextReadyFlag = false;	// ready to use again
		OnConsoleCmd( sText, this );
	}

	EXC_SET_BLOCK("ResyncCommand");
	if ( m_fResyncRequested != nullptr )
	{
		if ( !m_fResyncPause )
		{
			SetResyncPause(true, m_fResyncRequested);
			SetResyncPause(false, m_fResyncRequested);
		}
		else SetResyncPause(false, m_fResyncRequested, true);
		m_fResyncRequested = nullptr;
	}

	EXC_SET_BLOCK("SetTime");
	SetValidTime();	// we are a valid game server.

	ProfileTask overheadTask(PROFILE_OVERHEAD);

	if ( m_timeShutdown > 0 )
	{
		EXC_SET_BLOCK("shutdown");
		if ( g_World.GetTimeDiff(m_timeShutdown) <= 0 )
			SetExitFlag(2);
	}

	EXC_SET_BLOCK("generic");
	g_Cfg.OnTick(false);
	m_hdb.OnTick();
	EXC_CATCH;
}

bool CServer::Load()
{
	EXC_TRY("Load");

	EXC_SET_BLOCK("print sphere infos");
	g_Log.Event(LOGM_INIT, "%s.\n", g_szServerDescription);
#ifdef __GITREVISION__
	g_Log.Event(LOGM_INIT, "Compiled at %s (%s) [build %d / GIT hash %s]\n\n", __DATE__, __TIME__, __GITREVISION__, __GITHASH__);
#else
	g_Log.Event(LOGM_INIT, "Compiled at %s (%s)\n\n", __DATE__, __TIME__);
#endif
#ifdef _NIGHTLYBUILD
	static lpctstr pszNightlyMsg = "\r\n"
		"-----------------------------------------------------------------\r\n"
		"This is a nightly build of SphereServer. This build is to be used for testing and/or bug reporting ONLY.\r\n"
		"DO NOT run this build on a live shard unless you know what you are doing!\r\n"
		"Nightly builds are automatically made at every commit to the source code repository and might\r\n"
		"contain errors, might be unstable or even destroy your shard as they are mostly untested!\r\n"
		"-----------------------------------------------------------------\r\n\r\n";

	g_Log.Event(LOGL_WARN|LOGF_CONSOLE_ONLY, pszNightlyMsg);
#endif

#ifdef _WIN32
	EXC_SET_BLOCK("init winsock");
	tchar * wSockInfo = Str_GetTemp();
	if ( !m_SocketMain.IsOpen() )
	{
		WSADATA wsaData;
		int err = WSAStartup(MAKEWORD(2,2), &wsaData);
		if ( err )
		{
			if ( err == WSAVERNOTSUPPORTED )
			{
				err = WSAStartup(MAKEWORD(1,1), &wsaData);
				if ( err )
					goto nowinsock;
			}
			else
			{
nowinsock:		g_Log.Event(LOGL_FATAL|LOGM_INIT, "Winsock 1.1 not found!\n");
				return false;
			}
			sprintf(wSockInfo, "Using WinSock ver %d.%d (%s)\n", HIBYTE(wsaData.wVersion), LOBYTE(wsaData.wVersion), wsaData.szDescription);
		}
	}
#endif

	EXC_SET_BLOCK("loading ini");
	if (!g_Cfg.LoadIni(false))
		return false;

#ifdef _NIGHTLYBUILD
	g_Log.Event(LOGL_WARN|LOGF_LOGFILE_ONLY, pszNightlyMsg);
	if (!g_Cfg.m_bAgree)
	{
		g_Log.Event(LOGL_ERROR,"Please write AGREE=1 in Sphere.ini file to acknowledge that\nyou understand the risks of using nightly builds.\n");
		return false;
	}
#endif

#ifdef _WIN32
	if (wSockInfo[0])
		g_Log.Event(LOGM_INIT, wSockInfo);
#endif

	if (g_Cfg.m_bMySql && g_Cfg.m_bMySqlTicks)
	{
		EXC_SET_BLOCK( "Connecting to MySQL server" );
		if (m_hdb.Connect())
			g_Log.Event( LOGM_NOCONTEXT, "MySQL connected to server: '%s'.\n", g_Cfg.m_sMySqlHost.GetPtr() );
		else
		{
			g_Log.EventError( "Can't connect to MySQL DataBase. Check your ini settings or if the server is working.\n" );
			return false;
		}
	}

	EXC_SET_BLOCK("setting signals");
	SetSignals();

    if (g_Cfg.m_iAutoProcessPriority != 0)
    {
        EXC_SET_BLOCK("setting process priority");
        bool fPrioritySuccess = SetProcessPriority(g_Cfg.m_iAutoProcessPriority);
        g_Log.Event(LOGM_INIT, "Setting process priority... %s.\n", fPrioritySuccess ? "Success" : "Failed");
    }

	EXC_SET_BLOCK("loading scripts");
	TriglistInit();
	if ( !g_Cfg.Load(false) )
		return false;

	EXC_SET_BLOCK("init encryption");
	if ( m_ClientVersion.GetClientVer() )
	{
		char szVersion[128];
		g_Log.Event(LOGM_INIT, "ClientVersion=%s\n", static_cast<lpctstr>(m_ClientVersion.WriteClientVer(szVersion)));
		if ( !m_ClientVersion.IsValid() )
		{
			g_Log.Event(LOGL_FATAL|LOGM_INIT, "Bad Client Version '%s'\n", szVersion);
			return false;
		}
	}
	
#ifdef _WIN32
    EXC_SET_BLOCK("finalizing");
    g_NTWindow.SetWindowTitle();
#endif

	return true;
	EXC_CATCH;
	return false;
}
