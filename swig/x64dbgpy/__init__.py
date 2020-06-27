__author__ = 'Tomer Zait (RealGame)'
__version__ = '1.0.0'

from . import hooks
from . import pluginsdk
from . import __registers
from . import __events
from . import __flags
from . import __breakpoints

Register = __registers.Register()
Event = __events.Event()
Flag = __flags.Flag()
Breakpoint = __breakpoints.Breakpoint(Event)
