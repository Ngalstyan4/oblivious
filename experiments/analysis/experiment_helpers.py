import pandas as pd
import numpy as np
import plotly.express as px
from datetime import datetime


def get_components_of_runtime(table, name="unnamed"):
    fig = px.area(table[["Eviction Time",
                         "Baseline minor PF Time",
                         "Extra Minor PF Time",
                         "Major PF Time",
                         "Baseline User Time",
                         "Extra User Time",
                                         ]]/1e6,
                  title='Components of runtime(%s)'%name,
                  color_discrete_sequence=['#636efa', '#ef553b',  '#9e1700','#00cc96', '#ab63fa', '#3c0c73'])
    fig.update_layout(
        xaxis_title="Ratio",
        yaxis_title="Time(seconds)",
    )
    fig.add_trace(px.line(table["Measured(wallclock) runtime"]).data[0])
    fig.add_trace(px.line(table["sys+usr"] / 1e6).data[0])

    def anno(text, posx = 1.2, posy=0.32):
        dy = -0.04
        if anno.counter > 0:
            posx += 0.15
        fig.add_annotation(text=text,
              xref="paper", yref="paper",
              x=posx, y=posy + dy * anno.counter, showarrow=False)
        anno.counter+= 1
    anno.counter = 0

    anno("Workload constants:")
    anno("Baseline System Time(s): %.2f" % (table["Baseline System Time"].values[0]/1e6))
    anno("Baseline App Time(s): %.2f" % (table["Baseline App Time(us)"].values[0] / 1e6))
    anno("Baseline Minor PF Time(us): %.2f" % table["Baseline Single Minor PF Time(us)"].values[0])

    return fig

def get_experiment_data(EXPERIMENT_TYPES, experiment_name, experiment_dir):

    # get experiment data
    get_table = lambda experiment_type, table: pd.read_csv("%s/%s/%s/%s_results.csv" % (experiment_dir, experiment_name, experiment_type, table))
    tmp_tables = []
    for exp_type in EXPERIMENT_TYPES:
        cgroup = get_table(exp_type, "cgroup").set_index("RATIO")
        ftrace = get_table(exp_type, "ftrace").set_index("RATIO")

        # in multithreaded apps info is collected per cpu, so let's average it
        # N.B. todo:: does not work well for everything. would be good to add up number of
        # faults,
        ftrace = ftrace.groupby(["RATIO"]).sum()
        time_and_swap = get_table(exp_type, "time_and_swap").set_index("RATIO")
        experiment_final = ftrace.join(cgroup).join(time_and_swap)
        tmp_tables.append(experiment_final)

    """
    Column Legend
    SWAPIN_* : comes from ftrace, tracks calls to swapin_readahead function and closely measures # of major page faults
    EVICT_*  : comes from ftrace, tracks calls to try_to_free_mem_cgroup_pages. is not used ......
    NUM_FAULTS,NUM_MAJOR_FAULTS: comes from cgroup memory.stat, counts major+minor fault counts
    USER, SYSTEM, WALLCLOCK: from /usr/bin/time
    PAGES_EVICTED,PAGES_SWAPPED_IN : comes from fastswap NIC counters
    """
    return tmp_tables


def augment_tables(tables, filter_raw=True):
    # Old analysis approach and constants here `
    # https://docs.google.com/spreadsheets/d/1BFv4SqslgQpumk15-HiY504Ef5F1qNx8gFhKVX7-UEk/edit#gid=83666396

    for tbl in tables:

        # get some baseline workload constants
        WORKLOAD_CONSTANTS = {
            "Baseline System Time": tbl[tbl.index == 100]["SYSTEM"].values[0] * 1e6,
            "Baseline User Time": tbl[tbl.index == 100]["USER"].values[0] * 1e6,
            "Baseline Single Minor PF Time(us)": tbl[tbl.index == 100]["PAGE_FAULT_TIME"].values[0] /
                                          tbl[tbl.index == 100]["PAGE_FAULT_HIT"].values[0],
        }


        tbl[["Page Faults"]]             = tbl[["PAGE_FAULT_HIT"]].fillna(0)
        tbl[["PF Time(us)"]]             = tbl[["PAGE_FAULT_TIME"]].fillna(0)
        # can also be "NUM_MAJOR_FAULTS"
        tbl[["Major Page Faults"]]       = tbl[["SWAPIN_HIT"]].fillna(0)
        tbl[["Minor Page Faults"]]       = tbl["NUM_FAULTS"] - tbl["NUM_MAJOR_FAULTS"]
        tbl[["Evictions"]]               = tbl[["PAGES_EVICTED"]].fillna(0)


        # deprecate this..does not consider fastswap offload..
        #tbl[["Eviction Time(us)"]]      = tbl[["Evictions"]] * SYSTEM_CONSTANTS["SYSTEM_TIME_PER_EVICTION"]
        tbl[["Eviction Time"]]           = tbl[['EVICT_TIME']].fillna(0)

        tbl[["Major PF Time"]]       = tbl["SWAPIN_TIME"].fillna(0)
       # todo:: sth wrong with Sync time since:
       #    it is called by do_page_fault and its time should be included in minor fault time
       #    it is not present in linux_prefetching baseline
       #    ----- so, extra minor PF time should be AT LEAST sync time
       # conclusion does not hold since at times SYNC time is 0.6 sec and extra minor fault time is 0.5
       # tbl[["Tape Sync Time(us)"]]      = tbl[["SYNC_TIME"]].fillna(0)
        tbl[["Baseline minor PF Time"]]  = tbl[["Minor Page Faults"]] * WORKLOAD_CONSTANTS["Baseline Single Minor PF Time(us)"]

#        tbl[["Minor PF Time(us)"]]  Tape Sync Time(us)

        tbl[["Extra Minor PF Time"]] = (tbl["PF Time(us)"] - tbl["Major PF Time"] -  tbl["Baseline minor PF Time"]).clip(0)

        tbl[["Extra User Time"]]         = tbl["USER"] * 1e6 - WORKLOAD_CONSTANTS["Baseline User Time"]

        tbl[["System Overhead"]]         = tbl["Baseline minor PF Time"] + \
                                           tbl["Extra Minor PF Time"] + \
                                           tbl["Eviction Time"] + \
                                           tbl["Major PF Time"]

        tbl[["Total System Time"]]       = tbl["System Overhead"] + \
                                           WORKLOAD_CONSTANTS["Baseline System Time"]

        def to_seconds(a):
            pt = datetime.strptime(a,'%M:%S.%f')
            total_seconds = pt.microsecond * 1e-6 + pt.second + pt.minute*60 + pt.hour*3600
            return total_seconds
        tbl[["Measured(wallclock) runtime"]] = tbl["WALLCLOCK"].map(to_seconds)

        tbl[["Runtime"]]                 = tbl[["Measured(wallclock) runtime"]]
        tbl[["sys+usr"]]                 = tbl["USER"] * 1e6 + tbl["SYSTEM"] * 1e6
        tbl[["Runtime w/o Evictions"]]   = tbl["Runtime"] - tbl["Eviction Time"]/1e6

        if filter_raw:
            raw_cols = [c for c in tbl.columns.values if c.upper() == c]
            tbl.drop(columns=raw_cols, inplace=True)

        degr = lambda c: tbl[c] / tbl[tbl.index == 100][c].values[0]

        tbl[["Degradation"]]             = degr("Runtime")
        tbl[["Degradation w/o Evictions"]]= degr("Runtime w/o Evictions")

        # add baseline constants to the tables
        for (key,value) in WORKLOAD_CONSTANTS.items():
            tbl[[key]] = value

        tbl["Baseline App Time(us)"] =  tbl[tbl.index == 100]["Measured(wallclock) runtime"].values[0] * 1e6

    return tables

def take_column_named(column_name, named_dfs):
    res = pd.DataFrame()

    for name,df in named_dfs:
        res["(%s)%s" % (name,column_name)] = df[column_name]
    return res

