# VCRExt
WIN32 Extension for Tcl/Tk that provides support for special process management services

The initial reason to start this development was a bad video capture driver that led to a hangup during shutdown whenever a video had been captured previously.
I found out that the latest video capture process did not stop correctly, but if that process was killed, the shutdown did not hang.

Since I make a lot with Tcl/Tk, I decided to develop an extension that supports what I needed:
- Commands to kill (a) process(es),
- Commands to register, unregister and start a service,
- Commands to execute, resume and terminate a suspended process,
- Commands to wait for (thread and process) handle(s) and to close these handles.

__Remark__:
<ul>
<li/> The kill command can be used to kill a process via its process ID or its executable name. In addition, three killing levels can be specified:
<ul>
<li/> Level 0: Only kill the specified process.
<li/> Level 1: Kill the specified process and all processes created by that process.
<li/> Level 2: Kill the specified process and all processes created by that process recursively.
</ul>
<li/> The commands to register, unregister and start a service can be used to create a service implemented completely in the Tcl language. This is helpful to
perform actions whenever the system shuts down.
<li/> Since creation of a new process is not possible during shutdown, the command to create a suspended process can be invoked previously, e.g. when the service
starts up. During shutdown, this process can be resumed to do what it shall do. The wait and close commands should be used to wait until the command terminates and
for cleanup.
</ul>

__Implementation Details__:

The implementation base in VCRExtMain.h and VCRExtMain.cpp provides some macros and C++ classes that allow easy string, integer and pointer (handle) handling and
contains no extension specific code.

The command implementations in Commands.cpp use the implementation base and contain all extension specific coding.
