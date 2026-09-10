"""Analyze a PulseTailProbe capture without relabelling battery telemetry as instantaneous power."""
import argparse,csv,json,math,statistics
from pathlib import Path

def sustained_below(rows, end, threshold):
    """Require continuous valid DC samples; never bridge errors or large gaps."""
    tail = [r for r in rows if r['elapsed_s'] >= end]
    for r in tail:
        window = [x for x in tail if r['elapsed_s'] <= x['elapsed_s'] <= r['elapsed_s'] + 3]
        if not window or window[-1]['elapsed_s'] - r['elapsed_s'] < 2.4:
            continue
        if any(b['elapsed_s'] - a['elapsed_s'] > 1.1 for a, b in zip(window, window[1:])):
            continue
        if all(x['battery_error'] == '0' and x['ac_status'] == '0'
               and math.isfinite(x['discharge_w']) and x['discharge_w'] < threshold
               for x in window):
            return r['elapsed_s'] - end
    return None

def analyze(folder):
    rows=list(csv.DictReader((folder/'samples.csv').open(encoding='utf-8')))
    events=list(csv.DictReader((folder/'events.csv').open(encoding='utf-8')))
    for r in rows:
        for key in ('elapsed_s','discharge_w','voltage_v','cpu_busy_percent','query_ms'):
            r[key]=float(r[key])
    start=next((float(x['elapsed_s']) for x in events if x['event']=='load_start'),None)
    end=next((float(x['elapsed_s']) for x in events if x['event']=='load_joined'),None)
    if start is None or end is None:raise ValueError('Capture does not contain a completed workload')
    good=[r for r in rows if r['battery_error']=='0' and r['ac_status']=='0' and math.isfinite(r['discharge_w'])]
    baseline=[r['discharge_w'] for r in good if start-15<=r['elapsed_s']<start]
    tail=[r for r in good if r['elapsed_s']>=end]
    if not baseline or len(tail)<2:
        raise ValueError('Insufficient valid battery baseline or recovery samples')
    settled=statistics.median([r['discharge_w'] for r in tail[-30:]])
    zero=next((r['elapsed_s']-end for r in tail if r['boost_error']=='0' and r['boost']=='0'),None)
    low_cpu=next((r['elapsed_s']-end for r in tail if math.isfinite(r['cpu_busy_percent']) and r['cpu_busy_percent']<5),None)
    changes=[];previous=None
    for r in good:
        if r['raw_rate']!=previous:changes.append(r['elapsed_s']);previous=r['raw_rate']
    intervals=[b-a for a,b in zip(changes,changes[1:])]
    # A descriptive first-order fit, not identification of the hardware or proof of smoothing.
    peak=max(tail,key=lambda r:r['discharge_w'])
    fit=[r for r in tail if peak['elapsed_s']<=r['elapsed_s']<=end+45 and r['discharge_w']-settled>0.6]
    tau=r2=None
    if len(fit)>4:
        x=[r['elapsed_s']-end for r in fit];y=[math.log(r['discharge_w']-settled) for r in fit]
        xm=statistics.mean(x);ym=statistics.mean(y);den=sum((v-xm)**2 for v in x)
        slope=sum((a-xm)*(b-ym) for a,b in zip(x,y))/den
        if slope<0:tau=-1/slope
        total=sum((v-ym)**2 for v in y)
        if total:r2=1-sum((b-(ym+slope*(a-xm)))**2 for a,b in zip(x,y))/total
    result={'capture':folder.name,'samples':len(rows),'battery_errors':sum(r['battery_error']!='0' for r in rows),'source_changed':any(r['ac_status']!='0' for r in rows),
            'load_start_s':start,'load_end_s':end,'baseline_median_w':statistics.median(baseline),'settled_median_w':settled,
            'observed_peak_w':max(r['discharge_w'] for r in good),'post_work_peak_delay_s':peak['elapsed_s']-end,
            'boost_disabled_after_end_s':zero,'cpu_below_5pct_after_end_s':low_cpu,
            'reported_discharge_below_10w_s':sustained_below(rows,end,10),'reported_discharge_below_8w_s':sustained_below(rows,end,8),
            'raw_rate_change_interval_median_s':statistics.median(intervals) if intervals else None,
            'descriptive_decay_tau_s':tau,'log_decay_r_squared':r2,'query_median_ms':statistics.median(r['query_ms'] for r in rows),
            'hwinfo_available':any(r['hwinfo_error']=='0' for r in rows),
            'interpretation':'Battery rate is reported telemetry, not an independent instantaneous whole-system power measurement. A decay fit cannot distinguish filtering from a physical tail by itself.'}
    (folder/'analysis.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result,rows,start,end

def plot(folder,rows,start,end):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(3,1,figsize=(11,8),sharex=True,layout='constrained')
    t=[r['elapsed_s'] for r in rows]
    axes[0].step(t,[r['discharge_w'] for r in rows],where='post',label='Windows battery-reported discharge',color='#1976a3')
    axes[0].axhline(8,color='#888888',linestyle=':',label='8 W reference');axes[0].set_ylabel('Reported watts');axes[0].legend(loc='upper right')
    axes[1].step(t,[r['voltage_v'] for r in rows],where='post',color='#b2621a');axes[1].set_ylabel('Battery voltage (V)')
    axes[2].plot(t,[r['cpu_busy_percent'] for r in rows],color='#3c8051',label='System CPU activity (% of all logical CPUs)')
    axes[2].step(t,[float(r['boost']) if r['boost_error']=='0' else math.nan for r in rows],where='post',color='#af3860',label='Windows boost policy index (0 = disabled, 4 = efficient aggressive)')
    axes[2].set_ylabel('CPU % / policy index');axes[2].set_xlabel('Seconds since capture started');axes[2].legend(loc='upper right',fontsize=8)
    for ax in axes:ax.axvspan(start,end,color='#e9b747',alpha=.18);ax.axvline(end,color='#555555',linestyle='--',linewidth=1);ax.grid(alpha=.18)
    fig.suptitle('CPU work stops; battery-reported discharge decays gradually\n'+folder.name,fontsize=13)
    fig.savefig(folder/'tail.png',dpi=160);plt.close(fig)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('capture',type=Path);parser.add_argument('--plot',action='store_true');args=parser.parse_args()
    result,rows,start,end=analyze(args.capture)
    if args.plot:plot(args.capture,rows,start,end)
    print(json.dumps(result,indent=2))
