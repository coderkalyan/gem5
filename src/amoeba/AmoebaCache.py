from m5.params import *
from m5.proxy import *
from m5.objects.ClockedObject import ClockedObject


class AmoebaCache(ClockedObject):
    type = "AmoebaCache"
    cxx_header = "amoeba/amoeba_cache.hh"
    cxx_class = "AmoebaCache";

    cpu_side = VectorResponsePort("CPU side port, receives requests")
    mem_side = RequestPort("Memory side port, sends requests")

    latency = Param.Cycles(1, "Cycles taken on a hit or to resolve a miss")
    size = Param.MemorySize('64kB', "Cache size")
    system = Param.System(Parent.any, "The system this cache is part of")
