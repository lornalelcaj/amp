try:
    import pandas as pd
except ImportError as e:
    raise ImportError(
        "pandas not found. Make sure you run plot.py "
        "with the correct Python environment."
    ) from e
import plotly.express as px
import plotly.io as pio
from pathlib import Path


columns = [
    "queue_type", "n_threads", "config", "batch_size", "time_limit",
    "total_values", "throughput", "enq_count", "deq_count",
    "failed_deq_count", "duration_ns", "freelist_pushes",
    "freelist_pops", "freelist_max_size", "malloc_count",
    "reused_count", "successful_CAS_ops", "failed_CAS_ops"
]

queue_type_mapping = {
    0: "Sequential Queue",
    1: "Local Lock Queue",
    2: "Global Lock Queue",
    3: "Local Split Lock Queue",
    4: "Global Split Lock Queue",
    5: "Lock Free Queue",
    6: "Unordered Queue"
}

data_files = [
    "sequential_queue.data",
    "concurrent_queue_Q1.data",
    "concurrent_queue_Q2.data",
    "concurrent_queue_Q3.data",
    "concurrent_queue_Q4.data",
    "concurrent_queue_Q5.data",
    "concurrent_queue_Q6.data"
]

# --- Find the newest small-bench_date folder ---
base_path = Path("./data")
bench_folders = [f for f in base_path.iterdir() if f.is_dir() and f.name.startswith("small-bench_")]
if not bench_folders:
    raise FileNotFoundError("No folder starting with 'small-bench_' found.")

# Get the newest folder by modification time
latest_folder = max(bench_folders, key=lambda f: f.stat().st_mtime)

dfs = []
for file_name in data_files:
    file_path = latest_folder / file_name
    if file_path.exists():
        df_temp = pd.read_csv(file_path, delim_whitespace=True, names=columns, header=0)
        dfs.append(df_temp)
    else:
        print(f"Warning: {file_name} not found in {latest_folder}")

# Combine all DataFrames
df = pd.concat(dfs, ignore_index=True)

# Apply filters
filtered_df = df[
    (df["batch_size"] == 1000) &
    (df["time_limit"] == 1)
]

df_sequential = []
# Load sequential a second time afterwards to skip being filtered at a later point
file_path = latest_folder / "sequential_queue.data"
if file_path.exists():
    df_sequential = pd.read_csv(file_path, delim_whitespace=True, names=columns, header=0)
else:
    print(f"Warning: sequential_queue.data not found in {latest_folder}")

# Apply filters
filtered_sequential_df = df_sequential[
    (df_sequential["batch_size"] == 1000) &
    (df_sequential["time_limit"] == 1)
]


# Convert queue_type to string for cleaner legend
filtered_df = filtered_df.copy()
filtered_df["queue_type"] = filtered_df["queue_type"].map(queue_type_mapping)

# Recalculate throughput
filtered_df["throughput_recalc"] = (
    (filtered_df["enq_count"] + filtered_df["deq_count"] - filtered_df["failed_deq_count"])
    / filtered_df["duration_ns"]
) * 1e9  # convert from ns to seconds


# Get the throughput of the sequential queue
seq_rows = filtered_df[
    (filtered_df["queue_type"] == "Sequential Queue") &
    (filtered_df["n_threads"] == 1)
]

if seq_rows.empty:
    raise ValueError("No 1-thread Sequential Queue baseline found")

seq_throughput = seq_rows["throughput_recalc"].iloc[0]



# Compute speedup for all rows relative to sequential baseline
filtered_df["speedup"] = filtered_df["throughput_recalc"] / seq_throughput

# Add failed dequeue percentage column
filtered_df["failed_deq_pct"] = (
    filtered_df["failed_deq_count"] / (filtered_df["deq_count"])
) * 100

filtered_df["CAS_success_pct"] = (
    filtered_df["successful_CAS_ops"] /
    (filtered_df["successful_CAS_ops"] + filtered_df["failed_CAS_ops"])
) * 100

filtered_a_df = filtered_df[
    (filtered_df["config"] == "a")
]
# Add sequential back into the df's
filtered_a_df = pd.concat([filtered_sequential_df, filtered_a_df], ignore_index=True)

filtered_b_df = filtered_df[
    (filtered_df["config"] == "b")
]
# Add sequential back into the df's
filtered_b_df = pd.concat([filtered_sequential_df, filtered_b_df], ignore_index=True)

filtered_c_df = filtered_df[
    (filtered_df["config"] == "c")
]
# Add sequential back into the df's
filtered_c_df = pd.concat([filtered_sequential_df, filtered_c_df], ignore_index=True)

filtered_d_df = filtered_df[
    (filtered_df["config"] == "d")
]
# Add sequential back into the df's
filtered_d_df = pd.concat([filtered_sequential_df, filtered_d_df], ignore_index=True)

cas_a_df = filtered_a_df[
    filtered_a_df["queue_type"].isin([
        "Lock Free Queue",
        "Unordered Queue"
    ])
]
cas_b_df = filtered_b_df[
    filtered_b_df["queue_type"].isin([
        "Lock Free Queue",
        "Unordered Queue"
    ])
]
cas_c_df = filtered_c_df[
    filtered_c_df["queue_type"].isin([
        "Lock Free Queue",
        "Unordered Queue"
    ])
]
cas_d_df = filtered_d_df[
    filtered_d_df["queue_type"].isin([
        "Lock Free Queue",
        "Unordered Queue"
    ])
]

free_a_df = filtered_a_df[
    filtered_a_df["queue_type"].isin([
        "Global Lock Queue",
        "Global Split Lock Queue",
        "Unordered Queue"
    ])
]
free_b_df = filtered_b_df[
    filtered_b_df["queue_type"].isin([
        "Global Lock Queue",
        "Global Split Lock Queue",
        "Unordered Queue"
    ])
]
free_c_df = filtered_c_df[
    filtered_c_df["queue_type"].isin([
        "Global Lock Queue",
        "Global Split Lock Queue",
        "Unordered Queue"
    ])
]
free_d_df = filtered_d_df[
    filtered_d_df["queue_type"].isin([
        "Global Lock Queue",
        "Global Split Lock Queue",
        "Unordered Queue"
    ])
]




def save (df, y, ylabel, title, log, name):
    fig = px.bar(
        df,
        x="n_threads",
        y=y,
        color="queue_type",
        barmode="group",
        labels={
            "n_threads": "Number of Threads",
            y: ylabel,
            "queue_type": "Queue Type"
        },
        title=title
    )
    fig.update_traces(width=0.1)
    fig.update_layout(
        bargap=0.2,
        bargroupgap=0.1,
        xaxis_type="category"
    )
    # Set log scale for y-axis
    if log: fig.update_yaxes(type="log")

    Path("plot").mkdir(parents=True, exist_ok=True)
    fig.write_image("plot/"+name, scale=2)


def main():
    print("Saving plots")
    save(filtered_a_df, "throughput_recalc", "Throughput (ops/sec in Log)", "Throughput (Configuration A)", True, "throughput_plot_a.png")
    print("1")
    save(filtered_b_df, "throughput_recalc", "Throughput (ops/sec in Log)", "Throughput (Configuration B)", True, "throughput_plot_b.png")
    print("2")
    save(filtered_c_df, "throughput_recalc", "Throughput (ops/sec in Log)", "Throughput (Configuration C)", True, "throughput_plot_c.png")
    print("3")
    save(filtered_d_df, "throughput_recalc", "Throughput (ops/sec in Log)", "Throughput (Configuration D)", True, "throughput_plot_d.png")
    print("4")
    save(filtered_a_df, "duration_ns", "Duration in ns (Log)", "Duration (Configuration A)", True, "time_plot_a.png")
    print("5")
    save(filtered_b_df, "duration_ns", "Duration in ns (Log)", "Duration (Configuration B)", True, "time_plot_b.png")
    print("6")
    save(filtered_c_df, "duration_ns", "Duration in ns (Log)", "Duration (Configuration C)", True, "time_plot_c.png")
    print("7")
    save(filtered_d_df, "duration_ns", "Duration in ns (Log)", "Duration (Configuration D)", True, "time_plot_d.png")
    print("8")
    save(filtered_a_df, "speedup", "Speedup in %", "Speedup (Configuration A)", False, "speed_plot_a.png")
    print("9")
    save(filtered_b_df, "speedup", "Speedup in %", "Speedup (Configuration B)", False, "speed_plot_b.png")
    print("10")
    save(filtered_c_df, "speedup", "Speedup in %", "Speedup (Configuration C)", False, "speed_plot_c.png")
    print("11")
    save(filtered_d_df, "speedup", "Speedup in %", "Speedup (Configuration D)", False, "speed_plot_d.png")
    print("12")
    save(filtered_a_df, "failed_deq_pct", "% of Failed Dequeues", "% of Failed Dequeue Operations (Configuration A)", False,  "deq_plot_a.png")
    print("13")
    save(filtered_b_df, "failed_deq_pct", "% of Failed Dequeues", "% of Failed Dequeue Operations (Configuration B)", False, "deq_plot_b.png")
    print("14")
    save(filtered_c_df, "failed_deq_pct", "% of Failed Dequeues", "% of Failed Dequeue Operations (Configuration C)", False,  "deq_plot_c.png")
    print("15")
    save(filtered_d_df, "failed_deq_pct", "% of Failed Dequeues", "% of Failed Dequeue Operations (Configuration D)", False, "deq_plot_d.png")
    print("16")
    save(cas_a_df, "CAS_success_pct", "CAS Success (%)", "Compare And Swap (Configuration A)",False, "cas_plot_a.png")
    print("17")
    save(cas_b_df, "CAS_success_pct", "CAS Success (%)", "Compare And Swap (Configuration B)",False, "cas_plot_b.png")
    print("18")
    save(cas_c_df, "CAS_success_pct", "CAS Success (%)", "Compare And Swap (Configuration C)",False, "cas_plot_c.png")
    print("19")
    save(cas_d_df, "CAS_success_pct", "CAS Success (%)", "Compare And Swap (Configuration D)",False, "cas_plot_d.png")
    print("20")
    save(cas_a_df, "freelist_max_size", "Max Size (Log)", "Maximum size of Freelist (Configuration A)",True, "freelist_plot_a.png")
    print("21")
    save(cas_b_df, "freelist_max_size", "Max Size (Log)", "Maximum size of Freelist (Configuration B)",True, "freelist_plot_b.png")
    print("22")
    save(cas_c_df, "freelist_max_size", "Max Size (Log)", "Maximum size of Freelist (Configuration C)",True, "freelist_plot_c.png")
    print("23")
    save(cas_d_df, "freelist_max_size", "Max Size (Log)", "Maximum size of Freelist (Configuration D)",True, "freelist_plot_d.png")
    print("24")
    print("Everything saved")

if __name__ == "__main__":
    main()
