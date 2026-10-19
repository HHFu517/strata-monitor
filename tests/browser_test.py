"""Browser policy regression; standard-library only, never launches a browser/server."""
from pathlib import Path
import ast
import sys
from types import SimpleNamespace
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
checks = 0

def check(value, name):
    global checks
    assert value, name
    checks += 1

host = (ROOT / 'background_host.py').read_text(encoding='utf-8')
tree = ast.parse(host)
run = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'run')
check(not any(isinstance(n, ast.Constant) and n.value == '--open' for n in ast.walk(run)), 'managed host never supplies --open')
body = next(n for n in run.body if isinstance(n, ast.Try)).body
start = next(i for i, n in enumerate(body) if isinstance(n, ast.Import) and any(x.name == 'webbrowser' for x in n.names))
end = next(i for i, n in enumerate(body[start:], start) if isinstance(n, ast.Assign) and any(isinstance(t, ast.Attribute) and isinstance(t.value, ast.Name) and t.value.id == 'sys' and t.attr == 'argv' for t in n.targets))
policy = compile(ast.Module(body=body[start:end + 1], type_ignores=[]), str(ROOT / 'background_host.py'), 'exec')
for setting in (None, False, True):
    cfg = {'host': '127.0.0.1'}
    if setting is not None:
        cfg['open_browser'] = setting
    browser = SimpleNamespace(open=Mock(side_effect=AssertionError('real browser called')))
    events = Mock(side_effect=AssertionError('automatic Browser event signaled'))
    fake_sys = SimpleNamespace(path=[], argv=[])
    env = {'cfg': cfg, 'port': 65001, 'server': ROOT / 'fake-server.py', 'root': ROOT,
           'expected_config': ROOT / 'fake-config.json', 'sys': fake_sys,
           'os': SimpleNamespace(chdir=Mock()), 'K': SimpleNamespace(SetEvent=events)}
    with patch.dict(sys.modules, {'webbrowser': browser}):
        exec(policy, env)
        check('--open' not in fake_sys.argv, f'no --open for open_browser={setting}')
        check(fake_sys.argv[1:] == ['--engine', 'strata', '--config', str(env['expected_config']), '--host', cfg['host'], '--port', '65001'], 'other server arguments unchanged')
        for url in ('http://127.0.0.1:65001/', 'http://localhost:65001/', 'https://example.invalid/'):
            check(browser.open(url) is False, 'automatic browser request suppressed')
        check(not events.called, 'no Browser event emitted')
main = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
check('pendingBrowser' not in main, 'no deferred automatic browser state')
handler = main.split('case WM_SERVICE_BROWSER:', 1)[1].split('case WM_TRAY:', 1)[0]
check('return 0;' in handler and 'Open(' not in handler and 'UpdateLifecycle(' not in handler, 'legacy browser message safely ignored')
check('else if(id==OpenWeb && ModelReady(view.packet))Open(' in main, 'manual ready-gated open retained')
check('OpenWeb,L"打开网页"' in main, 'manual menu entry retained')
service = (ROOT / 'src/service.cpp').read_text(encoding='utf-8')
check('OpenEventW(SYNCHRONIZE,FALSE,Name(L"Browser",run)' in service, 'legacy Browser recovery retained')
check("'Local" in host and "StrataMonitor.Browser." in host, 'host Browser handle compatibility retained')
print(f'{checks} browser policy checks passed')
