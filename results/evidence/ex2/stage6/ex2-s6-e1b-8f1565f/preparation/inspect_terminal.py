"""Read-only terminal control handoff, not an independent scientific audit."""
import json
from collections import Counter
from pathlib import Path
import subprocess

root = Path.cwd()
identity = 'ex2-s6-e1b-8f1565f'
local = root / 'results/local'
execution = root / f'results/tmp/{identity}-execution'
terminal_path = execution / 'terminal.json'
if not terminal_path.is_file():
    raise RuntimeError('No retained terminal record; do not inspect live campaign or relaunch')
terminal = json.loads(terminal_path.read_text(encoding='utf-8'))
report = dict(terminal=terminal, ledger_presence={}, inspection_scope='terminal control facts and namespace inventory only')
ledger_base = local / f'{identity}-stage6-control.json'
for suffix in ['', '.incomplete', '.incomplete.tmp']:
    p = Path(str(ledger_base) + suffix)
    report['ledger_presence'][p.relative_to(root).as_posix()] = p.exists()
ledger = ledger_base if ledger_base.is_file() else Path(str(ledger_base) + '.incomplete')
if ledger.is_file():
    data = json.loads(ledger.read_text(encoding='utf-8'))
    fields = ['control_state','protocol_version','manifest_id','manifest_sha256','manifest_file_sha256',
              'campaign_start_time_utc','campaign_end_time_utc','fatal_sequence_index','fatal_reason','ledger_revision']
    report['control'] = {field:data.get(field) for field in fields}
    slots = data.get('slots', [])
    report['slot_count'] = len(slots)
    report['disposition_counts'] = dict(Counter(s.get('disposition') for s in slots))
    report['attempted_slots'] = sum(bool(s.get('process', {}).get('process_created')) for s in slots)
    report['dr45_cleanup_success_slots'] = sum(s.get('disposition') == 'resolved_success' and
        s.get('reconciliation_reason') == 'resolved_success_post_completion_cleanup' for s in slots)
    report['cell_analysis_anchors'] = [a for a in data.get('cell_analysis',[]) if a is not None]
    report['campaign_analysis_anchor'] = data.get('campaign_analysis')
    report['cell_analysis_anchor_count'] = len(report['cell_analysis_anchors'])
    report['cell_analysis_anchors_files_present'] = all((root / a['relative_path']).is_file()
        for a in report['cell_analysis_anchors'])
    campaign = report['campaign_analysis_anchor']
    report['campaign_analysis_anchor_file_present'] = bool(campaign and (root / campaign['relative_path']).is_file())
names = sorted(local.glob(f'{identity}*'))
children = [p for p in names if p.name.startswith(f'{identity}-slot-')]
report['namespace_inventory'] = {
    'child_final_directories': sum(p.is_dir() and not p.name.endswith('.incomplete') for p in children),
    'child_staging_paths': sum(p.name.endswith('.incomplete') for p in children),
    'child_failure_sidecars': sum(p.name.endswith('.failure.json') for p in children),
    'matching_local_paths': [p.relative_to(root).as_posix() for p in names],
    'analysis_files': sorted(p.relative_to(root).as_posix()
        for p in (local / f'{identity}-stage6-analysis').glob('*')),
    'execution_wrapper_files': sorted(p.name for p in execution.iterdir()),
}
report['stdout'] = (execution / 'stdout.log').read_text(encoding='utf-8', errors='replace')[-8000:]
report['stderr'] = (execution / 'stderr.log').read_text(encoding='utf-8', errors='replace')[-8000:]
commands = {'head':['rev-parse','--verify','HEAD'], 'branch':['branch','--show-current'],
            'status':['status','--porcelain=v1','--untracked-files=all'], 'diff_check':['diff','--check'],
            'cached_name_status':['diff','--cached','--name-status']}
report['final_git'] = {}
for key, args in commands.items():
    result = subprocess.run(['git', *args], check=True, capture_output=True, text=True)
    report['final_git'][key] = result.stdout.strip()
print(json.dumps(report, sort_keys=True, indent=2))
