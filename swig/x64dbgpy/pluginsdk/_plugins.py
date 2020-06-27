from . import x64dbg

def _to_str(text):
    try:
        if isinstance(text, bytes):
            return text.decode('utf-8')
        if isinstance(text, str):
            return text
        return repr(text)
    except Exception:
        return repr(text)


def _plugin_logprint(text=''):
    x64dbg._plugin_logprint(_to_str(text))


def _plugin_logputs(text=''):
    x64dbg._plugin_logputs(_to_str(text))
