from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject


class AmoebaCache(SimObject):
    type = "AmoebaCache"
    cxx_header = "amoeba/amoeba_cache.hh"

    inst_port = ResponsePort("CPU side port, receives requests")
    data_port = ResponsePort("CPU side port, receives requests")
    mem_side = RequestPort("Memory side port, sends requests")

    # latency = Param.Cycles(1, "Cycles taken on a hit or to resolve a miss")
    # size = Param.MemorySize('64kB', "Cache size")
    # system = Param.System(Parent.any, "The system this cache is part of")
