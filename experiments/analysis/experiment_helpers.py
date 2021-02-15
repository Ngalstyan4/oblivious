import pandas as pd
import numpy as np
import plotly.express as px

def get_experiment_table(named_tables, name):
    for (table_name, table) in named_tables:
        if table_name == name:
            return table

def get_components_of_runtime(table, name="(unnamed)"):
    fig = px.area(table[["Eviction Time(us)(cgroup func)",
                                         "Minor PF Time(us)",
                                         "Major PF Time(us)",
                                         "Total User Time" ]]/1e6,
                  title='Components of runtime(%s)'%name)
    fig.update_layout(
        xaxis_title="Ratio",
        yaxis_title="Time(seconds)",
    )
    return fig

def get_experiment_data(EXPERIMENT_TYPES, experiment_name, experiment_dir):

    # get experiment data
    get_table = lambda experiment_type, table: pd.read_csv("%s/%s/%s/%s_results.csv" % (experiment_dir, experiment_name, experiment_type, table))
    tmp_tables = []
    for exp_type in EXPERIMENT_TYPES:
        cgroup = get_table(exp_type, "cgroup").set_index("RATIO")
        ftrace = get_table(exp_type, "ftrace").set_index("RATIO")
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

    linux_prefetching, no_prefetching = tables[0],tables[1]
    # in microsecond(us)
    WORKLOAD_CONSTANTS = {
        "Baseline System Time": linux_prefetching[linux_prefetching.index == 100]["SYSTEM"].values[0] * 1e6,
        "Baseline User Time": linux_prefetching[linux_prefetching.index == 100]["USER"].values[0] * 1e6,
    }

    # sourced from Chris' measurements here `
    # https://docs.google.com/spreadsheets/d/1BFv4SqslgQpumk15-HiY504Ef5F1qNx8gFhKVX7-UEk/edit#gid=83666396
    # all times in micro-seconds(us)
    SYSTEM_CONSTANTS = {
        "BATCH_SIZE": 64,
        "MINOR_PF_TIME": 1.081240453,
        "MAJOR_PF_TIME": 6.9536973,
        "SYSTEM_TIME_PER_EVICTION": 12.67520977,
        "User Time Per Evicted Batch (us)": 2 #27.15875611,
    }

    for tbl in tables:

        # can also be "NUM_MAJOR_FAULTS"
        tbl[["Major Page Faults"]]       = tbl[["SWAPIN_HIT"]].fillna(0)

        tbl[["Evictions"]]               = tbl[["PAGES_EVICTED"]].fillna(0)
        tbl[["Minor Page Faults"]]       = tbl["NUM_FAULTS"] - tbl["NUM_MAJOR_FAULTS"]

        tbl[["Eviction Time(us)"]]       = tbl[["Evictions"]] * SYSTEM_CONSTANTS["SYSTEM_TIME_PER_EVICTION"]
        tbl[["Eviction Time(us)(cgroup func)"]]       = tbl[['EVICT_TIME']].fillna(0)

        tbl[["Minor PF Time(us)"]]       = tbl[["Minor Page Faults"]] * SYSTEM_CONSTANTS["MINOR_PF_TIME"]
        tbl[["Major PF Time(us)"]]       = tbl["SWAPIN_TIME"].fillna(0)

        tbl[["Total User Time"]]         = tbl[["USER"]] * 1e6
        tbl[["additional usertime per eviction(us)"]] = ((tbl["USER"] - tbl[tbl.index == 100]["USER"].values[0])*1e6)/tbl["Evictions"]


        tbl[["System Overhead"]]         = tbl["Minor PF Time(us)"] + tbl["Eviction Time(us)"] + tbl["Major PF Time(us)"]
        tbl[["Total System Time"]]       = tbl["System Overhead"] + WORKLOAD_CONSTANTS["Baseline System Time"]

        # Estimated and actual runtime`
        tbl[["Runtime"]]                 = tbl["Total User Time"] + tbl["Total System Time"]
        tbl[["Runtime w/o Evictions"]]   = tbl["Runtime"] - tbl["Eviction Time(us)"]
        if "WALLCLOCK" in tbl.columns:
                t = tbl["WALLCLOCK"]
                def lam(a):
                    arr = [int(i) for i in [a.split(":")[1].split('.')][0]]
                    return arr[0]+arr[1]*0.01
                tbl[["Measured(wallclock) runtime"]] = tbl["WALLCLOCK"].map(lam)


        if filter_raw:
            raw_cols = [c for c in tbl.columns.values if c.upper() == c]
            tbl.drop(columns=raw_cols, inplace=True)

        degr = lambda c: tbl[c] / tbl[tbl.index == 100][c].values[0]

        tbl[["Degradation"]]             = degr("Runtime")
        tbl[["Degradation w/o Evictions"]]= degr("Runtime w/o Evictions")

    return tables

def take_column_from_dfs(column_name, named_dfs):
    res = pd.DataFrame()

    for name,df in named_dfs:
        res["(%s)%s" % (name,column_name)] = df[column_name]
    return res

