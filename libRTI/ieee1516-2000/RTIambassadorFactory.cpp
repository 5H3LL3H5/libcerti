// ----------------------------------------------------------------------------
// CERTI - HLA RunTime Infrastructure
// Copyright (C) 2002-2014  ONERA
//
// This file is part of CERTI-libRTI
//
// CERTI-libRTI is free software ; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation ; either version 2 of
// the License, or (at your option) any later version.
//
// CERTI-libRTI is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY ; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this program ; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA
//
// ----------------------------------------------------------------------------

#include <RTI/RTIambassadorFactory.h>
#include <memory>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif

#include "PrettyDebug.hh"
#include "RTIambassadorImplementation.h"

#include "M_Classes.hh"

#include "config.h"

rti1516::RTIambassadorFactory::RTIambassadorFactory()
{
}

rti1516::RTIambassadorFactory::~RTIambassadorFactory()
    throw ()
{
}

namespace {
    static PrettyDebug D1516("LIBRTI1516", __FILE__);
    static PrettyDebug G1516("GENDOC1516",__FILE__) ;

    struct Nix{};
    struct Win{};
    
#ifdef RTIA_USE_TCP
const bool USE_TCP = true;
#else
const bool USE_TCP = false;
#endif
}

using namespace rti1516;

struct HelperRealFactory{
        static certi::RTI1516ambPrivateRefs* get_privateRefs(certi::RTI1516ambassador* p){
                return p->privateRefs;
        }
};

template <class placeholder> class SysWrapper;
template <bool use_tcp> int get_descriptor(rti1516::RTIambassador* p_ambassador){return -1;}

template <class SystemType, bool use_tcp> struct RealFactory{

    rti1516::RTIambassador* p_ambassador;
public :
    RealFactory(rti1516::RTIambassador* ambassador):p_ambassador(ambassador) {}
    
    std::auto_ptr< rti1516::RTIambassador >
    createRTIambassador(std::vector< std::wstring > & args)
	throw (BadInitializationParameter,
	       certi::RTIinternalError) {
    
	std::auto_ptr< rti1516::RTIambassador > ap_ambassador(p_ambassador);
    
	int descriptor = get_descriptor<use_tcp>(p_ambassador);
	SysWrapper<SystemType>(p_ambassador).dosomething();
    
	certi::M_Open_Connexion req, rep ;
	req.setVersionMajor(CERTI_Message::versionMajor);
	req.setVersionMinor(CERTI_Message::versionMinor);

	G1516.Out(pdGendoc,"        ====>executeService OPEN_CONNEXION");
	//p_ambassador->privateRefs->executeService(&req, &rep);
        HelperRealFactory::get_privateRefs(p_ambassador)->executeService(&req, &rep);
	G1516.Out(pdGendoc,"exit  RTIambassador::RTIambassador");

	return ap_ambassador;
    }
};
//---------------------------------------------------------------

//---------------------------------------------------------------
template int get_descriptor<true> (rti1516::RTIambassador* p_ambassador) {
    int port = p_ambassador->privateRefs->socketUn->listenUN();
    if (port == -1) {
        D1516.Out( pdError, "Cannot listen to RTIA connection. Abort." );
        throw rti1516::RTIinternalError(L"Cannot listen to RTIA connection" );
    }
    return port;
}
template int get_descriptor<false> (rti1516::RTIambassador* p_ambassador) {
    int pipeFd = p_ambassador->privateRefs->socketUn->socketpair();
    if (pipeFd == -1) {
        D1516.Out( pdError, "Cannot get socketpair to RTIA connection. Abort." );
        throw rti1516::RTIinternalError( L"Cannot get socketpair to RTIA connection" );
    }
    return pipeFd;
}
    
std::vector<std::string> build_rtiaList(){
    std::vector<std::string> rtiaList;
    const char* env = getenv("CERTI_RTIA");
    if (env && strlen(env)) {
	rtiaList.push_back(std::string(env));
    }
    env = getenv("CERTI_HOME");
    if (env && strlen(env)) {
	rtiaList.push_back(std::string(env) + "/bin/rtia");
    }
            
    rtiaList.push_back(PACKAGE_INSTALL_PREFIX "/bin/rtia");
    rtiaList.push_back("rtia");
            
    return rtiaList;
}
//-----------------------------------------------------------
//Windows wrapper
template <>  class SysWrapper<Win> {
    template <bool use_tcp> void buildOut(stream&,wstring rtia, int port);
    rti1516::RTIambassador* p_ambassador;
public: 
    SysWrapper(rti1516::RTIambassador* ambassador):p_ambassador(ambassador) {}
    template <bool use_tcp> doSomething(p_ambassador, rtiaList){
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );

#ifndef RTIA_CONSOLE_SHOW
	/*
	 * Avoid displaying console window
	 * when running RTIA.
	 */
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
#endif

#if !defined(RTIA_USE_TCP)
	SOCKET newPipeFd;
	if (!DuplicateHandle(GetCurrentProcess(),
			     (HANDLE)pipeFd,
			     GetCurrentProcess(),
			     (HANDLE*)&newPipeFd,
			     0,
			     TRUE, // Inheritable
			     DUPLICATE_SAME_ACCESS)) {
	    D1516.Out( pdError, "Cannot duplicate socket for RTIA connection. Abort." );
	    throw rti1516::RTIinternalError( L"Cannot duplicate socket for RTIA connection. Abort." );
	}
#endif

	bool success = false;
	for (unsigned i = 0; i < rtiaList.size(); ++i) {
	    std::stringstream stream;
               
	    build<use_tcp>(stream, rtiaList[i], descriptor):
                
                // Start the child process.
                if (CreateProcess( NULL, // No module name (use command line).
				   (char*)stream.str().c_str(),	// Command line.
				   NULL,					// Process handle not inheritable.
				   NULL,					// Thread handle not inheritable.
				   TRUE,					// Set handle inheritance to TRUE.
				   0,   					// No creation flags.
				   NULL,					// Use parent's environment block.
				   NULL,					// Use parent's starting directory.
				   &si,					// Pointer to STARTUPINFO structure.
				   &pi ))					// Pointer to PROCESS_INFORMATION structure.
                {
                    success = true;
                    break;
                }
	}
	if (!success) {
	    msg << "CreateProcess - GetLastError()=<"
		<< GetLastError() <<"> "
		<< "Cannot connect to RTIA.exe";
	    throw rti1516::RTIinternalError(msg.str());
	}

	p_ambassador->privateRefs->handle_RTIA = pi.hProcess;

#if !defined(RTIA_USE_TCP)
	closesocket(pipeFd);
	closesocket(newPipeFd);
#endif
    }
};

template <> void SysWrapper<Win>::buildOut<true>(stream&,wstring rtia, newPipeFd){
    stream << rtia << ".exe -p " << port;
}
template <> void SysWrapper<Win>::buildOut<false>(stream&,wstring rtia, newPipeFd){
    stream << rtia << ".exe -f " << newPipeFd;
}
//----------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
template <> class SysWrapper<Nix> {
    template <bool use_tcp> closeAllFd(int descriptor);
    template <bool use_tcp> void execute(wstringstream& stream,int descriptor);
}
    rti1516::RTIambassador* p_ambassador;
public: 
SysWrapper(rti1516::RTIambassador* ambassador):p_ambassador(ambassador) {}
template <bool use_tcp> void doSomething(descriptor){

    sigset_t nset, oset;
    // temporarily block termination signals
    // note: this is to prevent child processes from receiving termination signals
    sigemptyset(&nset);
    sigaddset(&nset, SIGINT);
    sigprocmask(SIG_BLOCK, &nset, &oset);

    switch((p_ambassador->privateRefs->pid_RTIA = fork())) {
    case -1: // fork failed.
        perror("fork");
        // unbock the above blocked signals
        sigprocmask(SIG_SETMASK, &oset, NULL);
#if !defined(RTIA_USE_TCP)
        close(pipeFd);
#endif
        throw rti1516::RTIinternalError(wstringize() << "fork failed in RTIambassador constructor");
        break ;

    case 0: // child process (RTIA).
        closeAllFd<use_tcp>(descriptor);
        for (unsigned i = 0; i < rtiaList.size(); ++i)
        {
	    std::stringstream stream;
	    execute<use_tcp>(stream,descriptor);
        }
        
        // unbock the above blocked signals
        sigprocmask(SIG_SETMASK, &oset, NULL);
        msg << "Could not launch RTIA process (execlp): "
	    << strerror(errno)
	    << std::endl
	    << "Maybe RTIA is not in search PATH environment.";
        throw rti1516::RTIinternalError(msg.str().c_str());

    default: // father process (Federe).
        // unbock the above blocked signals
        sigprocmask(SIG_SETMASK, &oset, NULL);
#if !defined(RTIA_USE_TCP)
        close(pipeFd);
#endif
        break ;
    } //end fork
}//end do something

};

    
template <> SysWrapper<Nix>::closeAllFd<true>(int descriptor);
{
    // close all open filedescriptors except the pipe one
    for (int fdmax = sysconf(_SC_OPEN_MAX), fd = 3; fd < fdmax; ++fd) {
	if (fd == descriptor) {
	    continue;
	}
        close(fd);
    }       
}
template <> SysWrapper<Nix>::closeAllFd<false>(int );
{
    // close all open filedescriptors except the pipe one
    for (int fdmax = sysconf(_SC_OPEN_MAX), fd = 3; fd < fdmax; ++fd) {
        close(fd);
    }       
}
    
template <> void SysWrapper<Nix>::execute<true>(wstringstream& stream,int descriptor);
{
    stream << port;
    execlp(rtiaList[i].c_str(), rtiaList[i].c_str(), "-p", stream.str().c_str(), NULL);
}

template <> void SysWrapper<Nix>::execute<false>(wstringstream& stream,int descriptor);
{
    stream << pipeFd;
    execlp(rtiaList[i].c_str(), rtiaList[i].c_str(), "-f", stream.str().c_str(), NULL);
}
//-----------------------------------------------------------------------------
std::auto_ptr< rti1516::RTIambassador >
rti1516::RTIambassadorFactory::createRTIambassador(std::vector< std::wstring > & args)
    throw (BadInitializationParameter,
	   RTIinternalError)
{
    
    rti1516::RTIambassador* p_ambassador = new certi::RTI1516ambassador(); 	       

    G1516.Out(pdGendoc,"enter RTIambassador::RTIambassador");
    PrettyDebug::setFederateName( "LibRTI::UnjoinedFederate" );
    std::wstringstream msg;

    p_ambassador->privateRefs = new RTI1516ambPrivateRefs();
    p_ambassador->privateRefs->socketUn = new SocketUN(stIgnoreSignal);
    p_ambassador->privateRefs->is_reentrant = false ;

    std::vector<std::string> rtiaList = build_rtiaList();

#ifdef WIN32
    return RealFactory<Win,USE_TCP>(p_ambassador).createRTIambassador(args);
#else 
    return RealFactory<Nix,USE_TCP>(p_ambassador).createRTIambassador(args);
#endif       

}

//} // end namespace rti1516
