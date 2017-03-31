# ramulator-NVMain
    An Architectural Level Main Memory Simulator for 
Emerging Non-Volatile Memories

======================================================

Sections

    1. Overview
    2. Building ramulator-NVMain
	Trace Simulation
    3. Running ramulator-NVMain
    4. Configuring ramulator-NVMain
    5. Hacking ramulator-NVMain
    6. README Changelog

------------------------------------------------------  

1. Overview

    ramulator-NVMain is a cycle accurate main memory 
    simulator designed to simulate emerging non-volatile
    memories at the architectural level. Since the 
    current status of non-volatile memory is unknown
    and this is a research tool, flexability is provided
    to implement different variations of memory controllers, 
    interconnects, organizations, etc. Ramulator as our 
    processor, in CPU trace-driven mode. CPU traces are 
    collected using a Pintool. To model the stacked and 
    the off-chip DRAM memory we have integrated a detailed
    main memory simulator - NVMain. We use NVMain to obtain
    Latency and hit rate results of the DRAM cache subsystem. 
    We feed NVMain with DRAM command traces obtained from 
    our simulations using Ramulator. 
	
    Thanks for trying ramulator-NVMain!

------------------------------------------------------

2. Building ramulator-NVMain

    ramulator-NVMain can be build as a standalone executable to
    run trace-based simulations.

    Trace Simulation

    The trace simulation can be build using scons:

    $ scons --build-type=[release|debug]

    Compiling with scons will automatically set 
    the compile flags needed for trace-based
    simulation. You can use --build-type=debug
    to add debugging symbol, or release for 
    -03 optimization.


------------------------------------------------------

3. Running ramulator-NVMain


    NVMain can be run on the command line with trace-based
    simulation via:

    ./nvmain CONFIG_FILE TRACE_FILE 
	
    eg.
    cd ramulator-NVMain
    scons
    ./nvmain.fast Tests/Configs/LO.config ../Trace/403.gcc

    The CONFIG_FILE is the path to the configuration file
    for the memory system being simulated. The TRACE_FILE
    is the path to the trace file with the memory requests
    to simulate. Cycles is optional and specifies the max
    number of cycles to simulate. By default the entire
    trace file is simulated.


------------------------------------------------------

4. Configuring ramulator-NVMain


    ramulator-NVMain can be configured using the configuration files.
    Several example configuration files can be found in
    the Config/ folder in the ramulator-NVMain trunk. 


------------------------------------------------------

5. Hacking ramulator-NVMain


    As mentioned in the overview, ramulator-NVMain 
    is meant to be flexible. Writing your own 
    interconnect, memory controller, endurance model, 
    address translator, etc. Can be done by creating 
    a new C++ file with your new class.

    Each unit has a Factory class which selects 
    the class to used based on the configuration
    file input. You can create a class by looking
    at one of the example classes in each folder:

    MemControl - Custom Memory Controllers
    FaultModels - Custom hard-fault models
    Decoders - Custom address translators
    Endurance - Custom Endurance models
    Interconnect - Custom Interconnects
    Prefetchers - Custom prefetchers
    SimInterface - Simulator interface used to
                   gather useful statistics from
                   the CPU simulator such as
                   instructions executed, cache
                   miss rates, etc.
    traceReader - Custom trace file readers

    When adding a class, make sure to update
    the factory class to #include your class
    header and to initialize your class if 
    the configuration is set to your class'
    name.


------------------------------------------------------


9/08/2016 - Created first README

