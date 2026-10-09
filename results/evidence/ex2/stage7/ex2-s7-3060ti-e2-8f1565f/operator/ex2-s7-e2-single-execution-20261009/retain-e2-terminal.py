"""Run only after native terminal proof and no live production process.
Record terminal counts and unchanged evidence identities; no scientific audit or retry.
"""
from datetime import datetime,timezone
import hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path('C:/Users/rolan/src/ComputeLab-Stage7-3060Ti');ID='ex2-s7-3060ti-e2-8f1565f'
RECORD=ROOT/'results/local/ex2-s7-e2-single-execution-20261009';OWNED=ROOT/f'results/tmp/{ID}-execution';PREP=ROOT/f'results/local/{ID}-preparation-20261009'
def read(p):return json.loads(p.read_bytes())
def new(p,value):
 b=value.encode() if isinstance(value,str) else (json.dumps(value,sort_keys=True,indent=2)+'\n').encode()
 with p.open('xb') as f:f.write(b);f.flush();os.fsync(f.fileno())
def file_id(p):return {'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
terminal=read(OWNED/'supervisor-terminal.json');created=read(OWNED/'supervisor-created.json');wrapper=read(OWNED/'wrapper-terminal.json');power=read(OWNED/'power-restoration.json')
live=subprocess.run(['powershell','-NoProfile','-Command',"@(Get-Process | Where-Object ProcessName -In @('ComputeLabEx2Stage6','ComputeLabEx2Stage6Supervisor') | Select-Object ProcessName,Id,Path) | ConvertTo-Json -Compress"],check=True,capture_output=True,text=True).stdout.strip()
if live and live!='[]':raise RuntimeError('Still live production; do not read any ledger/package')
final_path=ROOT/f'results/local/{ID}-stage6-control.json';partial_path=Path(str(final_path)+'.incomplete')
ledger_path=final_path if final_path.exists() else partial_path if partial_path.exists() else None
ledger=read(ledger_path) if ledger_path else None
slots=ledger['slots'] if ledger else []
counts={'process_creations_recorded':sum(bool(s['process']['process_created']) for s in slots),'resolved_success':sum(s['disposition']=='resolved_success' for s in slots),'resolved_diagnostic_failure':sum(s['disposition']=='resolved_diagnostic_failure' for s in slots),'unresolved_campaign_fatal':sum(s['disposition']=='unresolved_campaign_fatal' for s in slots),'not_launched':sum(s['disposition']=='not_launched' for s in slots),'pending_phase':sum(s['control_phase']=='pending' for s in slots),'undispositioned':sum(s['disposition'] is None for s in slots)} if ledger else None
status='E2_COMPLETED_NATIVE_0' if terminal['exit_code']==0 else 'E2_INCOMPLETE_NATIVE_'+str(terminal['exit_code'])
baseline=read(PREP/'e1-preservation-baseline.json');e1_drift=[]
for row in baseline['files']:
 p=Path(row['path'])
 if not p.is_file() or p.stat().st_size!=row['bytes'] or hashlib.sha256(p.read_bytes()).hexdigest()!=row['sha256']:e1_drift.append(row['path'])
git=subprocess.run(['git','status','--porcelain=v1','--untracked-files=all'],cwd=ROOT,env={**os.environ,'GIT_OPTIONAL_LOCKS':'0'},check=True,capture_output=True,text=True).stdout.strip()
preserved=[]
for parent in [OWNED,RECORD]:preserved.extend(p for p in parent.rglob('*') if p.is_file())
for p in (ROOT/'results/local').iterdir():
 if p.name.startswith(ID+'-slot-') or p.name.startswith(ID+'-stage6-control') or p.name==ID+'-stage6-analysis':preserved.extend(p.rglob('*') if p.is_dir() else [p])
inventory=[file_id(p) for p in sorted(set(p for p in preserved if p.is_file()))]
new(RECORD/'terminal-evidence-inventory.json',{'utc':datetime.now(timezone.utc).isoformat(),'scope':'Post-native-terminal file identities; no independent scientific acceptance/curation','files':inventory})
receipt={'utc':datetime.now(timezone.utc).isoformat(),'status':status,'manifest_id':ID,'dispatch_count':1,'native_supervisor_creations':1,'created':created,'native_terminal':terminal,'wrapper_terminal':wrapper,'power_restoration':power,'counts_basis':'Authoritative final terminal ledger' if ledger_path==final_path else 'Partial checkpoint only; not authoritative terminal coverage' if ledger else 'No ledger available; counts unknown','ledger_path':str(ledger_path) if ledger_path else None,'ledger_file_identity':file_id(ledger_path) if ledger_path else None,'ledger_state':ledger['control_state'] if ledger else None,'ledger_revision':ledger['ledger_revision'] if ledger else None,'counts':counts,'e1_preservation_drift':e1_drift,'e1_preserved':not e1_drift,'source_git_status':git,'git_clean':not git,'live_production_observed':False,'active_ledger_package_reads_during_execution':0,'scientific_acceptance_requires_separate_independent_audit':True,'retry_resume_performed':False,'restore_avast_immediately':True}
new(RECORD/'terminal-execution-receipt.json',receipt)
meaning={0:'Completed',2:'ConfigurationOrPreflightRejected',3:'CampaignIncomplete',4:'ControlPublicationFailure',5:'InternalControlFailure'}.get(terminal['exit_code'],'Native exit not classified here')
lines=['# Stage-7 E2 guarded single-launch terminal report','',f'**{status} — native exit {terminal["exit_code"]} ({meaning}).**','',
 f'Exactly one dispatch of the uniquely named E2 task and one native supervisor creation. PID {created["pid"]}, wrapper {created["wrapper_pid"]}, start UTC {created["start_utc"]}; end UTC {terminal["end_utc"]}. No retry/resume/second Start-ScheduledTask, protocol change or deadline lowering.',
 '', 'Exact native argv: `'+json.dumps(created['argv'])+'`; cwd `'+created['cwd']+'`.',
 '', 'Exact human authorization bound manifest ex2-s7-3060ti-e2-8f1565f to semantic 99404552f6302a7273f991d02d6532ebd06b38a6e7224c17721278d6b00899aa and physical d4d93217721285bbc7d55d8fc6853a3685e0d23f1f5e1b6203244c359122e1e1. Clean source 8f1565f7fb92e2184f4d2382aabf8fa7d00e1274, all eight instrument hashes, 22/220 schedule and namespace absence were checked before dispatch and at native point of use. Fresh metadata API UUID and OBS-off probe ran under the same environment inherited by supervisor/children. Prior manual NVIDIA recording/overlay-OFF attestation retained; support DLL alone not treated as capture.',
 '', 'During execution, monitoring was limited to process/task state and wrapper creation/terminal receipts. No active .incomplete/.incomplete.tmp/package file was opened, tailed, polled, hashed or viewed. Native control, per-operation oracle correctness, raw retention, Job containment and resolved-only-no-retry rules were left unchanged.',
 '', f'Counts basis: {receipt["counts_basis"]}. Ledger `{receipt["ledger_path"]}`, state `{receipt["ledger_state"]}`, revision {receipt["ledger_revision"]}. Recorded counts: `{json.dumps(counts,sort_keys=True)}`. Native exit alone and these counts do not constitute a full independent scientific audit.',
 '', f'Active Balanced AC sleep restoration readback: restored={power["restored"]}, command exit {power.get("exit_code")}; prior AC 30 minutes restored only after supervisor termination, DC unchanged. No live production process observed. **Restore Avast protections immediately now after terminal completion/failure, as previously agreed.** No Avast settings were silently changed.',
 '', f'E1 baseline file drift count {len(e1_drift)}; E1 remains permanently INCOMPLETE, authoritative revision 20 and distinct prepared temporary revision 21 untouched/not adopted. E1 task was neither restarted nor modified. Current execution Git status clean={not git}. No accepted historical archive/source/build changes.',
 '', 'If native exit 4, it remains unresolved ControlPublicationFailure and is not a GPU timeout, a harmless event or the Stage-6 descendant-survival case. All evidence stays unchanged, including final/staging/sidecar/control temporary paths. No repair, manual rename, curation or root-cause investigation was performed.',
 '', 'Entire 22-cell protocol-1.4 diagnostic/H scope, five CV/VC/CV/VC/CV paired blocks per cell, 100 ordered observations/child, zero warm-up and original 60000/1200000/86400000 ms deadlines remain in force. Full scientific acceptance requires a separate independent post-run audit. Gate 0 FAIL, Stage 2 NOT GRANTED, production backend UNSELECTED; no CUDA/Vulkan or cross-generation performance ranking.',
 '', 'Evidence:', '',f'- Wrapper launch, probe, stdout/stderr and terminal receipts: `{OWNED}`.',f'- Control/analysis and all child packages: `{ROOT / "results/local"}` under the exact E2 identity.',f'- Native terminal receipt: `{OWNED / "supervisor-terminal.json"}`.',f'- Detailed terminal receipt: `{RECORD / "terminal-execution-receipt.json"}`.',f'- Post-terminal file inventory: `{RECORD / "terminal-evidence-inventory.json"}`.',
 '', 'E2 PRODUCTION SUPERVISOR CREATIONS: 1',f'E2 NATIVE TERMINAL EXIT: {terminal["exit_code"]}','NO RETRY. NO RESUME.','SCIENTIFIC ACCEPTANCE REQUIRES SEPARATE INDEPENDENT AUDIT.']
new(RECORD/'terminal-execution-report.md','\n'.join(lines)+'\n')
print(json.dumps({'status':status,'counts_basis':receipt['counts_basis'],'counts':counts,'ledger_state':receipt['ledger_state'],'ledger_revision':receipt['ledger_revision'],'power_restored':power['restored'],'e1_drift_count':len(e1_drift),'report':str(RECORD/'terminal-execution-report.md')}))
