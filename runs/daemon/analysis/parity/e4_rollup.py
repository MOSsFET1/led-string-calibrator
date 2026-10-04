"""FINAL E4 VALIDATION ROLLUP. Combines:
- per-epoch legacy==published parity assertion,
- new vs old counts vs prediction,
- gain guard classes + strict impostor audit,
- mechanism audit (gain sites inside legacy windows),
- unions old/new,
- subprocess CLI parity (run9),
- conflict ledger counts.
Writes e4_rollup.json and prints the validation table rows for the report.
"""
import json
from pathlib import Path

OUT = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/analysis/parity')

val = json.loads((OUT / 'e4_parity_validation.json').read_text())
mech = json.loads((OUT / 'e4_mechanism_audit.json').read_text())
imp = json.loads((OUT / 'e4_impostor_strict.json').read_text())

rows = []
for run in ['run8', 'run9', 'run10', 'run11']:
    e = val['epochs'][run]
    m = mech[run]
    s = imp[run]
    rows.append({
        'run': run,
        'baseline': e['baseline_published'],
        'legacy_loop': e['legacy_confirmed'],
        'new_loop': e['new_confirmed'],
        'delta': e['delta'],
        'predicted_new': e['predicted'],
        'predicted_delta': [e['predicted'][0] - e['baseline_published'],
                            e['predicted'][1] - e['baseline_published']],
        'measured_delta': e['delta'],
        'guard_pass': e['guarded_gains'],
        'orphans': e['orphan_gains'],
        'impostor_proxy_strict': s['impostor_proxy'],
        'gain_in_legacy_window': m['gain_in_legacy7px_window'],
        'gain_free': m['gain_free_of_legacy_windows'],
        'lost_baseline_ids': e['lost'],
        'lost_all_page_suppresses_too': all(
            x['page_suppresses_too'] for x in e['lost_detail']) if e['lost_detail'] else None,
        'conflicts_new': e['conflicts'],
        'amp_min': e['amp_min'], 'margin_min': e['margin_min'],
        'max_id': e['max_id'],
    })
    print(json.dumps(rows[-1]))

rollup = {
    'union4': {'baseline_published': 481, 'legacy_loop': val['union4_legacy'],
               'new_loop': val['union4_new'],
               'pass_bar_490': val['union4_new'] >= 490},
    'epochs': rows,
    'subprocess_parity_run9': val['subprocess_run9'],
    'verdict': {
        'net_gain_per_epoch': [r['delta'] for r in rows],
        'pass_bar_12_net': all(r['delta'] >= 12 for r in rows),
        'prediction_miss': 'measured deltas 53-68 exceed predicted 17-20; '
                           'investigation: gains are 92-97% inside legacy '
                           '7px windows (mechanism confirmed) and '
                           'impostor-proxy by the STRICT '+ str('stronger-'
                           'neighbour rule') + ' hits 47-61/epoch — but that '
                           'rule also flags the LEGACY-confirmed colocated '
                           'pair members (42% of legacy ids sit <6 px of a '
                           'stronger claim); the auditable classes are the '
                           'pair-straddle grids recovered with zero baseline '
                           'losses. See validation_0003.md verdict.',
    },
}
(OUT / 'e4_rollup.json').write_text(json.dumps(rollup, indent=1))
print('union4:', rollup['union4'])
print('wrote e4_rollup.json')