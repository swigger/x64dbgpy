import sys
import signal
import warnings
import builtins as __builtin__
import multiprocessing
from . pluginsdk import bridgemain, _plugins
from io import StringIO
import os

def __raw_input(prompt=''):
    return bridgemain.GuiGetLineWindow(prompt)


def __input(prompt=''):
    return eval(__raw_input(prompt))


def __signal(sig, action):
    warnings.warn('Cannot use signals in x64dbgpy...', UserWarning, stacklevel=2)


class OutputHook(object):
    def __init__(self, stream_name='stdout', callback=_plugins._plugin_logprint):
        self.is_hooking = False
        self.callback = callback

        self.stream_name = stream_name
        if self.stream_name not in ['stderr', 'stdout']:
            raise Exception('Cannot hook %s stream.' % self.stream_name)
        elif self.__is_hooked():
            raise Exception('Do not hook the hooker!')
        outf = getattr(sys, self.stream_name)
        if outf is None:
            # on windows, GUI app dont redirect stdout/err might has null sys.stdout/err
            outf = open(os.devnull, 'w')
        self.__original_stream = outf 

    def __getattr__(self, name):
        return getattr(self.__original_stream, name)

    def __is_hooked(self):
        stream = getattr(sys, self.stream_name)
        return hasattr(stream, 'is_hooking')

    def write(self, text):
        try:
            self.callback(text)
        except Exception as e:
            es = StringIO()
            print(e, file=es)
            _plugins._plugin_logprint(es.getvalue())

        # Hack to workaround a Windows 10 specific error.
        # IOError (errno=0 OR errno=9) occurs when writing to stderr or stdout after an exception occurs.
        # Issue: https://github.com/x64dbg/x64dbgpy/issues/31
        # See:
        #    https://bugs.python.org/issue32245
        #    https://github.com/Microsoft/console/issues/40
        #    https://github.com/Microsoft/vscode/issues/36630
        # Further issue discussion:
        #    https://github.com/x64dbg/x64dbgpy/pull/32
        try:
            self.__original_stream.write(text)
        except IOError as e:
            pass

    def start(self):
        if not self.is_hooking:
            setattr(sys, self.stream_name, self)
            self.is_hooking = True

    def stop(self):
        if self.is_hooking:
            setattr(sys, self.stream_name, self.__original_stream)
            self.is_hooking = False


def __inithooks():
    # Hook sys.stdout
    STDOUT_HOOK = OutputHook('stdout')
    STDOUT_HOOK.start()

    # Hook sys.stderr
    STDERR_HOOK = OutputHook('stderr')
    STDERR_HOOK.start()

    # Hook raw_input, input (stdin)
    # setattr(__builtin__, 'original_raw_input', __builtin__.raw_input)
    # setattr(__builtin__, 'raw_input', __raw_input)
    setattr(__builtin__, 'original_input', __builtin__.input)
    setattr(__builtin__, 'input', __input)

    # Set arguments
    sys.argv = [os.path.join(os.path.dirname(__file__), '__init__.py')]

    # Hook Signals (for pip and other signal based programs)
    setattr(signal, 'original_signal', signal.signal)
    setattr(signal, 'signal', __signal)

    # Fix Multiprocessing (Will not be able to use x64dbgpy lib for now...)
    multiprocessing.set_executable(os.path.join(sys.exec_prefix, 'pythonw.exe'))

    # Print Message That The Hooks Worked!
    print('[PYTHON] stdout, stderr, input hooked!')


# init hooks only when plugin is loaded in x64(32)dbg.exe.
# PyCharm would load this plugin to generate python skeleton code.
import _winapi
import re
mainexe = _winapi.GetModuleFileName(0)
mainexe = re.sub(r".*[\\/]", "", mainexe)
mainexe = mainexe.lower()

if mainexe == "x64dbg.exe" or mainexe == "x32dbg.exe":
    __inithooks()
