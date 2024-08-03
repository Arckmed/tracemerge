# tracemerge
Plugin to merge x64dbg trace files. Also includes a command to delete trace entries.

The commands are going to look for the trace files at the /db folder in your x64dbg directory.
## commands
``tracemerge`` - merge as many trace files you want

``tracedelete`` - delete entries from the trace file

## usage example
``tracemerge tracefile1, tracefile2, tracefile3``

Generates a trace file with the name output_merge_timestamp.traceXX using tracefile1 header and trace data + tracefile2 trace data + tracefile3 trace data.

``tracedelete tracefile, 0x539, 5AC``

Generates a trace file with the name output_delete_timestamp.traceXX with entries at 0x539 and 0x5AC index in the tracefile deleted.

### 3rd party libraries
[RSJp-cpp](https://github.com/subh83/RSJp-cpp)
