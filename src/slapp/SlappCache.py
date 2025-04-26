from m5.params import *
from m5.proxy import *
from m5.objects.ClockedObject import ClockedObject


class SlappCache(ClockedObject):
    type = "SlappCache"
    cxx_header = "slapp/slapp_cache.hh"
    cxx_class = "gem5::SlappCache";

    cpu_side = VectorResponsePort("CPU side port, receives requests")
    mem_side = RequestPort("Memory side port, sends requests")

    latency = Param.Cycles(1, "Cycles taken on a hit or to resolve a miss")
    sets = Param.Unsigned(256, "Number of sets")
    associativity = Param.Unsigned(4, "Associativity (number of tags per set)")
    capacity = Param.MemorySize('64kB', "Data capacity of the backing cache store")
    system = Param.System(Parent.any, "The system this cache is part of")
