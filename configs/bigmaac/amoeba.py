import m5
from m5.objects import *


class L1Cache(Cache):
    assoc = 4
    tag_latency = 1
    data_latency = 1
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20
    size = "64kB"


class Slapp(SlappCache):
    latency = 1
    sets = 256
    associativity = 4
    capacity = "64kB"

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()
system.mem_mode = "timing"
system.mem_ranges = [AddrRange("512MB")]
system.membus = SystemXBar()

# system.cpu = X86TimingSimpleCPU()
system.cpu = X86O3CPU()
system.slapp = SlappCache()
system.slapp.mem_side = system.membus.cpu_side_ports
# system.cpu.icache_port = system.slapp.inst_port
# system.cpu.dcache_port = system.slapp.data_port
# system.slapp.mem_side = system.membus.cpu_side_ports
system.cpu.icache = L1Cache()
system.cpu.icache.mem_side = system.membus.cpu_side_ports
system.cpu.icache.cpu_side = system.cpu.icache_port
system.cpu.dcache_port = system.slapp.cpu_side
# system.cpu.dcache = L1Cache()
# system.cpu.dcache.mem_side = system.membus.cpu_side_ports
# system.cpu.dcache.cpu_side = system.cpu.dcache_port
system.cpu.createInterruptController()
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

system.system_port = system.membus.cpu_side_ports

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

binary = "tests/test-progs/hello/bin/x86/linux/hello"
# binary = "tests/test-progs/slapp/soa/aos"
# binary = "/usr/bin/python3"
# script = "tests/test-progs/python/hello.py"
system.workload = SEWorkload.init_compatible(binary)
# system.workload = FSWorkload.init_compatible(binary)

process = Process()
process.cmd = [binary]
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
