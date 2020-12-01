/*
* Copyright 2020 Martin Conrad
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/
#include "VCRExtMain.h"
#include <windows.h>
#include <Tlhelp32.h>
#include <string.h>

/*
	Command version
	Syntax:
		version
	Returns value:
		String containing version no.
 */
DECLARE(version,0,"") {
	RES(String, ret);

	ret = "1.1";
}
FINISH
/*
	Command kill
	Syntax:
		kill id level
	Function:
		Kills specified process. Id is either a process id or the name
		of an executable file, e.g. notepad.exe. Level 0 specifies 
		only the specified process(es) will be killed, 1 the specified
		processes and all processes invoked by these processes will be
		killed, 2 the specified processes and all processes invoked by
		by these processes recursively.
	Returns:
		Number of processes that have been killed.
 */
DECLARE(kill, 2, "id level") {
	ARG(Int, level, 2);
	RES(Int, res);
	Int id(0);
	String name("");
	int count = 0;
	if (level < 0 || level > 2)
		throw ValueException(ValueException::ValueExceptionLimit, "Value out of range (0 - 2)");

	HANDLE hd = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (hd != INVALID_HANDLE_VALUE) {
		try {
			ARG(Int, s1, 1);
			id = s1;
		}
		catch (ValueException) {
			ARG(String, s1, 1);
			name = s1;
		}
		int res, i = 0, j, k;
		PROCESSENTRY32 ent;
		struct procvallist { int pid, ppid, del; } *procs;

		ent.dwSize = sizeof ent;
		for (res = Process32First(hd, &ent); res; res = Process32Next(hd, &ent))
			i++;
		procs = new procvallist[i];
		for (i = 0, res = Process32First(hd, &ent); res; i++, res = Process32Next(hd, &ent)) {
			procs[i].pid = ent.th32ProcessID;
			procs[i].ppid = ent.th32ParentProcessID;
			if (id && ent.th32ProcessID == id)
				procs[i].del = 1;
			else if (*((const char*)name)) {
				for (j = 0; ((const char*)name)[j] == (char)ent.szExeFile[j] && ent.szExeFile[j]; j++);
				procs[i].del = ent.szExeFile[j] == 0;
			}
			else
				procs[i].del = 0;
		}
		for (j = 0; level && j < i; ) {
			for (k = 0; k < i; k++) {
				if (!procs[j].del && procs[k].del && procs[j].ppid == procs[k].pid) {
					procs[j].del = 1;
					break;
				}
			}
			j += (k < i && level > 1) ? -j : 1;
		}
		CloseHandle(hd);
		for (j = 0; j < i; j++) {
			if (procs[j].del) {
				if (hd = OpenProcess(PROCESS_TERMINATE, FALSE, procs[j].pid)) {
					if (TerminateProcess(hd, 0))
						count++;
					CloseHandle(hd);
				}
			}
		}
	}
	res = count;
}
FINISH
/*
	Comand validpid
	Syntax:
		validpid pid
	Returns:
		1: pid is a valid process id
		0: pid is not a valid process id
 */
DECLARE(validpid, 1, "pid") {
	ARG(Int, s1, 1);
	RES(Int, res);
	HANDLE hd;

	if ((hd = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, s1)) ||
		(hd = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, s1))) {
		DWORD exitcode = 0;

		res = GetExitCodeProcess(hd, &exitcode) ? exitcode == STILL_ACTIVE : 1;
		CloseHandle(hd);
	}
	else if (GetLastError() == ERROR_ACCESS_DENIED)
		res = 1;
	else
		res = 0;
}
FINISH
/*
	Command regservice
	Syntax:
		regservice name description command starttype
	Function:
		Register program as a service
	Returns:
		0: program registered
		other: WIN32 error code when trying to register program
 */
DECLARE(regservice, 4, "name description command starttype") {
	ARG(String, name, 1);
	ARG(String, desc, 2);
	ARG(String, cmd, 3);
	ARG(Int, type, 4);
	RES(Int, res);
	SC_HANDLE hd, hds;

	if (type < 2 || type > 4)
		throw ValueException(ValueException::ValueExceptionLimit, "Value out of range (2 - 4");

	if((hd = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS)) == NULL)
		res = GetLastError();
	else if ((hds = CreateServiceA(hd, name, desc, SC_MANAGER_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, type, SERVICE_ERROR_IGNORE, cmd, NULL, NULL, NULL, NULL, "")) == NULL) {
		CloseServiceHandle(hd);
		res = GetLastError();
	}
	else {
		CloseServiceHandle(hds);
		CloseServiceHandle(hd);
		res =0;
	}
}
FINISH
/*
	Command unregservice
	Syntax:
		unregservice name
	Function:
		Unregister a service
	Returns:
		0: Service unregistered
		other: WIN32 error code when trying to unregister service
 */
DECLARE(unregservice, 1, "name") {
	ARG(String, name, 1);
	RES(Int, res);
	SC_HANDLE hd, hds;

	if((hd = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS)) == NULL)
		res = GetLastError();
	else if ((hds = OpenServiceA(hd, name, SC_MANAGER_ALL_ACCESS)) == NULL) {
		CloseServiceHandle(hd);
		res = GetLastError();
	}
	else {
		res = DeleteService(hds) ? 0 : GetLastError();
		CloseServiceHandle(hds);
		CloseServiceHandle(hd);
	}
}
FINISH
/*
	Service functions and structures
 */
static struct VcrExtSrv {
	Value *command;
	Tcl_Interp *ip;
	Tcl_AsyncHandler ah;
	HANDLE ehd, thd;
	DWORD type;
	SERVICE_STATUS_HANDLE shd;
	SERVICE_STATUS state;
	SERVICE_TABLE_ENTRY entry;
	VcrExtSrv() { entry.lpServiceName = NULL; command = NULL; }
} sd;
/*
	AsyncHandler to handle asynchronous events in stored tcl interpreter
 */
static int asynchand(ClientData, Tcl_Interp *, int rc) {
	Tcl_InterpState is = Tcl_SaveInterpState(sd.ip, rc);
	String cmd(**sd.command);

	cmd = cmd + String(Int(sd.type));
	Tcl_EvalObjEx(sd.ip, cmd, TCL_EVAL_DIRECT|TCL_EVAL_GLOBAL);
	SetEvent(sd.thd);
	return Tcl_RestoreInterpState(sd.ip, is);
}
/*
	Service thread, used to avoid blocking of main thread while service
	is running
 */
static DWORD WINAPI thmain(LPVOID) {
	sd.state.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	sd.state.dwCurrentState = SERVICE_START_PENDING;
	sd.ah = Tcl_AsyncCreate(asynchand, NULL);
	if (!StartServiceCtrlDispatcher(&sd.entry))
		sd.type = GetLastError();
	delete [] sd.entry.lpServiceName;
	CloseHandle(sd.ehd);
	Tcl_AsyncDelete(sd.ah);
	CloseHandle(sd.thd);
	sd.ehd = sd.thd = NULL;
	sd.entry.lpServiceName = NULL;
	return 0;
}
/*
	Service event handler, marks asynchronous event and waits until command
	has been finished by asynchronous handler.
 */
static DWORD WINAPI handler(DWORD type, DWORD, LPVOID, LPVOID) {
	if (type == SERVICE_CONTROL_INTERROGATE) {
		return NO_ERROR;
	}
	sd.type = type;
	Tcl_AsyncMark(sd.ah);
	WaitForSingleObject(sd.thd, INFINITE);
	sd.state.dwCheckPoint = sd.state.dwWaitHint = 0;
	switch (type) {
	case SERVICE_CONTROL_CONTINUE:
		sd.state.dwCurrentState = SERVICE_RUNNING;
		break;
	case SERVICE_CONTROL_PAUSE:
		sd.state.dwCurrentState = SERVICE_PAUSED;
		break;
	case SERVICE_CONTROL_PRESHUTDOWN:
	case SERVICE_CONTROL_SHUTDOWN:
	case SERVICE_CONTROL_STOP:
		sd.state.dwCurrentState = SERVICE_STOPPED;
	}
	SetServiceStatus(sd.shd, &sd.state);
	return 0;
}
/*
	Service main function.
 */
static VOID WINAPI smain(DWORD, LPTSTR *) {
	if (sd.shd = RegisterServiceCtrlHandlerEx(sd.entry.lpServiceName, handler, NULL)) {
		sd.state.dwCheckPoint = sd.state.dwWaitHint = 0;
		sd.state.dwCurrentState = SERVICE_RUNNING;
		if (SetServiceStatus(sd.shd, &sd.state) ||
			(sd.state.dwControlsAccepted & SERVICE_ACCEPT_PRESHUTDOWN && (sd.state.dwControlsAccepted &= ~SERVICE_ACCEPT_PRESHUTDOWN, SetServiceStatus(sd.shd, &sd.state))))
			WaitForSingleObject(sd.ehd, INFINITE);
	}
}
/*
	Command serve
	Syntax:
		serve name bitmask command
	Function:
		Enter service. name is the service name or the service to
		be invoked. bitmask specifies which service events will be
		created. Each time a service will be requested, command
		will be invoked with the type flag as its parameter.
	Returns:
		0: OK
		1: Service is just running
		2: Not enough memory
		other: Win32 error code
 */
DECLARE(serve, 3, "name bitmask command") {
	ARG(String, name, 1);
	ARG(Int, mask, 2);
	ARG(String, cmd, 3);
	RES(Int, res);

	if (mask & ~(SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_PAUSE_CONTINUE|SERVICE_ACCEPT_SHUTDOWN|SERVICE_ACCEPT_PRESHUTDOWN))
		throw ValueException(ValueException::ValueExceptionLimit, "Invalid bit mask value");
	res = 1;
	if (sd.entry.lpServiceName == NULL) {
		sd.command = new String(cmd + " ");
		if (sd.entry.lpServiceName = new wchar_t[strlen(name) + 1]) {
			mbstowcs(sd.entry.lpServiceName, name, strlen(name) + 1);
			sd.entry.lpServiceProc = smain;
			sd.state.dwControlsAccepted = mask;
			if (sd.ehd = CreateEvent(NULL, FALSE,FALSE, NULL)) {
				if (sd.thd = CreateEvent(NULL, FALSE,FALSE, NULL)) {
					HANDLE thd;
					sd.ip = ip;
					if (thd = CreateThread(NULL, 0, thmain, NULL, 0, NULL)) {
						CloseHandle(thd);
						res = 0;
					}
				}
			}
			if (res == 1) {
				res = GetLastError();
				delete [] sd.entry.lpServiceName;
				if (sd.ehd)
					CloseHandle(sd.ehd), sd.ehd = NULL;
				if (sd.thd)
					CloseHandle(sd.thd), sd.thd = NULL;
			}
		}
		else
			res = 2;
	}
}
FINISH
/*
	Command execsuspended
	Syntax:
		execsuspended command
	Function:
		Creates a new process which starts in suspended state, command
		is the command line.
	Returns:
		List containig process and thread handle, if successful. Otherwise
		WIN32 error code.
 */
DECLARE(execsuspended, 1, "command") {
	ARG(String, s1, 1);
	RES(String, res);
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	char *cl = new char[strlen(s1) + 1];

	strcpy(cl, s1);
	memset(&si, 0, sizeof si);
	memset(&pi, 0, sizeof pi);
	si.cb = sizeof si;
	// si.lpDesktop = "WinSta0";
	pi.dwThreadId = CreateProcessA(NULL, cl, NULL, NULL, False, CREATE_SUSPENDED, NULL, NULL, &si, &pi);
	delete [] cl;
	if (!pi.dwThreadId)
		res = String(Int(GetLastError()));
	else
		res = String(PtrValue((Tcl_WideInt) pi.hProcess)) + " " + String(PtrValue((Tcl_WideInt)pi.hThread));
}
FINISH
/*
	Command resume
	Syntax:
		resume thandle
	Fuction:
		Resumes suspended process. thandle is the thread handle returned
		by execsuspended.
	Returns:
		Return code from WIN32 resume function, in error case -WIN32 error
		code.
 */
DECLARE(resume, 1, "threadhandle") {
	ARG(PtrValue, s1, 1);
	RES(Int, res);

	res = ResumeThread((HANDLE)(int) s1);
	if (res == -1)
		res = -(int)GetLastError();
}
FINISH
/*
	Command terminate
	Syntax:
		terminate handle exitcode
	Function:
		Terminates the process specified by handle. exitcode
		is the exit code to be used.
	Return:
		0: OK
		other: WIN32 error code
 */
DECLARE(terminate, 2, "handle exitcode") {
	ARG(PtrValue, s1, 1);
	ARG(Int, s2, 2);
	RES(Int, res);
	
	if (TerminateProcess((HANDLE)(Tcl_WideInt)s1, s2))
		res = 0;
	else
		res = GetLastError();
}
FINISH
/*
	Command wait
	Syntax:
		wait handle
	Function:
		Waits until process or thread has been finished, depending on
		what kind of handle handle is.
		In case handle is a list of handles, wait waits until the first
		handle has been signalled.
	Returns:
		< 0: -WIN32 error code
		other: Index of 1st handle in handle that has been signalled.
 */
DECLARE(wait, 1, "handle") {
	RES(Int, res);
	try {
		ARG(PtrValue, hd, 1);

		if (WaitForSingleObject((HANDLE)(Tcl_WideInt)hd, INFINITE) == WAIT_FAILED)
			res = -(int)GetLastError();
		else
			res = 0;
	}
	catch (ValueException) {
		// List has been given
		Tcl_Obj **hdos;
		int count;

		if (Tcl_ListObjGetElements(ip, objs[1], &count, &hdos) == TCL_ERROR)
			throw ValueException(ValueException::TypeMismatch, "No list object");
		if (count >= MAXIMUM_WAIT_OBJECTS)
			throw ValueException(ValueException::Overflow, "List too long (max. 64) entries");
		HANDLE *hds = new HANDLE[count];
		int i;

		try {
			for (i = 0; i < count; i++)
				hds[i] = (HANDLE)(Tcl_WideInt) PtrValue(*hdos[i]);
		}
		catch (ValueException e) {
			delete [] hds;
			throw e;
		}
		i = WaitForMultipleObjects(count, hds, FALSE, INFINITE) & 0x7f;
		delete [] hds;
		res = i == 0x7f ? -(int)GetLastError() : i;
	}
}
FINISH
/* 
	Command close
	Syntax:
		close handle
	Function:
		Closes all handles in handle.
	Returns:
		No. of handles closed successfully
 */
DECLARE(close, 1, "handle") {
	RES(Int, res);
	try {
		ARG(PtrValue, val, 1);

		res = CloseHandle((HANDLE)(Tcl_WideInt) val) ? 1 : 0;
	}
	catch (ValueException) {
		// not int, must be list
		Tcl_Obj **hdos;
		int count;

		if (Tcl_ListObjGetElements(ip, objs[1], &count, &hdos) == TCL_ERROR)
			throw ValueException(ValueException::TypeMismatch, "No list object");
		res = 0;
		while (count > 0) {
			if (CloseHandle((HANDLE)(Tcl_WideInt)PtrValue(*hdos[--count])))
				res++;
		}
	}
}
FINISH
/*
Command help
Syntax:
help
Returns value:
String containing help for all commands provided by VCRExt.
*/
DECLARE(help, -1, "") {
	RES(String, ret);
	String cmd("");
	bool found = false;
	if (cnt == 1) {
		ret = "The VCREXT extension provides the following commands:\n";
	}
	else if (cnt == 2) {
		cmd = String(objs[1]);
		ret = "";
	}
	while (!found) {
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "close") == 0) {
			ret += "  Command close\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::close <handle>\n";
			ret += "    Description:\n";
			ret += "      This command closes handles previously returned by command execsuspended.\n";
			ret += "      <handle> must be either one of the values returned by execsuspended or a\n";
			ret += "      list of values returned by execsuspended.\n";
			ret += "      Keep in mind: Not to close any handle returned by execsuspended prevents\n";
			ret += "      the corresponding system resource from being freed. However, closing any\n";
			ret += "      handle twice can have unpredictable effects, from getting a system error\n";
			ret += "      code to an application crash.\n";
			ret += "      \n";
			ret += "      Close returns the number of handles it could close. This should be the\n";
			ret += "      number of handles specified by <handle>.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "execsuspended") == 0) {
			ret += "  Command execsuspended\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::execsuspended <command>\n";
			ret += "    Description:\n";
			ret += "      Creates a new process which starts in suspended state. <command>\n";
			ret += "      specifies the command line to be executed in the native syntax, e.g\n";
			ret += "      with \\ as file separator.\n";
			ret += "      \n";
			ret += "      Returns a list containing the process handle and the handle of the\n";
			ret += "      thread in case of success. Otherwise the Windows error code.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "help") == 0) {
			ret += "  Command help\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::help [<name>]\n";
			ret += "    Description:\n";
			ret += "      If no <name> parameter has been specified, this command returns\n";
			ret += "      help texts for all commands provided by this extension. If <name>\n";
			ret += "      has been specified, the help text for the specified command will\n";
			ret += "      be returned.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "kill") == 0) {
			ret += "  Command kill\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::kill <id> <level>\n";
			ret += "    Description:\n";
			ret += "      Kills the process specified by <id>. <id> must be either a process ID\n";
			ret += "      or the name of a executable file, e.g. tclsh.exe.\n";
			ret += "      <level> specifies how kill works. Allowed values for <level> are 0, 1\n";
			ret += "      and 2. Depending on <level>, kill works as follows:\n";
			ret += "        <level> = 0: Only the specified process will be killed.\n";
			ret += "        <level> = 1: The specified process and all child processes will be\n";
			ret += "                     killed.\n";
			ret += "        <level> = 2: The specified process and all child processes will be\n";
			ret += "                     killed recursively.\n";
			ret += "      \n";
			ret += "      Returns the number of processes that have been killed.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "regservice") == 0) {
			ret += "  Command regservice\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::regservice <name> <description> <command> <starttype>\n";
			ret += "    Description:\n";
			ret += "      Registers a service with name <name> and a describing text specified\n";
			ret += "      by <description>. The command that invokes the service will be passed\n";
			ret += "      as 3rd parameter <command>. <starttype> must be a value between 2 and\n";
			ret += "      4 and specifies the so-called service start option:\n";
			ret += "        2: Auto-start service, will be started by the service control\n";
			ret += "           manages at system startup.\n";
			ret += "        3: On-demand service, will be started by the service control\n";
			ret += "           manager when a process invokes the StartService function.\n";
			ret += "        4: Disabled service, will not be started.\n";
			ret += "      \n";
			ret += "      Returns 0 on success and the Windows error code otherwise.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "resume") == 0) {
			ret += "  Command resume\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::resume <handle>\n";
			ret += "    Description:\n";
			ret += "      Resumes a suspended process. <handle> must be a thread handle\n";
			ret += "      returned by an execsuspended command.\n";
			ret += "      \n";
			ret += "      Returns the suspend count returned by the WIN32 function ResumeThread\n";
			ret += "      or the WIN32 error code in error case.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "serve") == 0) {
			ret += "  Command serve\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::serve <name> <bitmask> <command>\n";
			ret += "    Description:\n";
			ret += "      Starts the service control dispatcher in a new thread. <name>\n";
			ret += "      specifies the name of the service as specified in regservice,\n";
			ret += "      <bitmask> specifies the control codes the service accepts (logical\n";
			ret += "      OR combination of one or more of the following values:\n";
			ret += "        0x1:   Service can be stopped,\n";
			ret += "        0x2:   Service can be paused or continued,\n";
			ret += "        0x4:   Service handles shutdown notifications,\n";
			ret += "        0x100: Service handles prioritized shutdown notifications)\n";
			ret += "      and <command> is the command to be executed by the service control\n";
			ret += "      dispatcher whenever a supported action shall be performed. The\n";
			ret += "      control code will be appended, therefore it is recommended to specify\n";
			ret += "      the name of a tcl procedure that acceps one integer parameter, the\n";
			ret += "      control code. The control code can have one of the following values:\n";
			ret += "        0x1:  The service shall stop,\n";
			ret += "        0x2:  The sercice shall be paused,\n";
			ret += "        0x3:  The service shall be continued,\n";
			ret += "        0x5:  The service shall stop due to system shutdown,\n";
			ret += "        0xF:  The service shall stop due to system shutdown (prioritized).\n";
			ret += "      \n";
			ret += "      Returns 0 on success, 1 if service is running, 2 if not enough memory\n";
			ret += "      is available or any WIN32 error code.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "terminate") == 0) {
			ret += "  Command terminate\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::terminate <handle> <exitcode>\n";
			ret += "    Description:\n";
			ret += "      Terminates the process specified by <handle>. <handle> must be a\n";
			ret += "      process handle returned by an execsuspended command. <exitcode> is\n";
			ret += "      the exit code of the process to be terminated.\n";
			ret += "      \n";
			ret += "      Returns 0 on success and a WIN32 error code otherwise.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "unregservice") == 0) {
			ret += "  Command unregservice\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::unregservice <name>\n";
			ret += "    Description:\n";
			ret += "      Unregister a service. <name> specifies the service name as specified\n";
			ret += "      in the Windows registry. The name specified in a previous regservice\n";
			ret += "      command is an example for such a name.\n";
			ret += "      \n";
			ret += "      Returns 0 on success and a WIN32 error code otherwise.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "validpid") == 0) {
			ret += "  Command validpid\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::validpid <pid>\n";
			ret += "    Description:\n";
			ret += "      Checks whether <pid> can be used as process ID.\n";
			ret += "      \n";
			ret += "      Returns 1 if <pid> can be used as process ID, 0 otherwise.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "version") == 0) {
			ret += "  Command version\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::version\n";
			ret += "    Description:\n";
			ret += "      This command returns the version of the extension as a string.\n";
			ret += "\n";
			found = true;
		}
		if (((const char*)cmd)[0] == 0 || strcmp(cmd, "wait") == 0) {
			ret += "  Command wait\n";
			ret += "    Syntax:\n";
			ret += "      VCRExt::wait <handle>\n";
			ret += "    Description:\n";
			ret += "      This command waits until one of the threads or processes specified by\n";
			ret += "      <handle> has been terminated. <handle> is either one of the values\n";
			ret += "      returned by a previously called execsuspended command or a list of\n";
			ret += "      max. 64 values returned by several execsuspended commands.\n";
			ret += "      \n";
			ret += "      Returns the index of the first handle of a thread or process that has\n";
			ret += "      been terminated. In error case, the WIN32 error code will be returned\n";
			ret += "      with negative sign.\n";
			ret += "\n";
			found = true;
		}
		if (!found) {
			ret = String("Command ") + cmd + " not supported.\nThe VCREXT extension provides the following commands:\n";
			cmd = "";
		}
	}
}
FINISH
static NewCmdDesc versionDesc("::VCRExt::version", version, NULL, NULL);
static NewCmdDesc killDesc("::VCRExt::kill", kill, NULL, NULL);
static NewCmdDesc validpidDesc("::VCRExt::validpid", validpid, NULL, NULL);
static NewCmdDesc regserviceDesc("::VCRExt::regservice", regservice, NULL, NULL);
static NewCmdDesc unregserviceDesc("::VCRExt::unregservice", unregservice, NULL, NULL);
static NewCmdDesc serveDesc("::VCRExt::serve", serve, NULL, NULL);
static NewCmdDesc execsuspendedDesc("::VCRExt::execsuspended", execsuspended, NULL, NULL);
static NewCmdDesc resumeDesc("::VCRExt::resume", resume, NULL, NULL);
static NewCmdDesc terminateDesc("::VCRExt::terminate", terminate, NULL, NULL);
static NewCmdDesc waitDesc("::VCRExt::wait", wait, NULL, NULL);
static NewCmdDesc closeDesc("::VCRExt::close", close, NULL, NULL);
static NewCmdDesc helpDesc("::VCRExt::help", help, NULL, NULL);
