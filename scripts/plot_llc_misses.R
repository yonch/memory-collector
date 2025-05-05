#!/usr/bin/env Rscript

# Setup - load required libraries
if (!requireNamespace("nanoparquet", quietly = TRUE)) {
  install.packages("nanoparquet")
}
if (!requireNamespace("ggplot2", quietly = TRUE)) {
  install.packages("ggplot2")
}
if (!requireNamespace("dplyr", quietly = TRUE)) {
  install.packages("dplyr")
}
if (!requireNamespace("tidyr", quietly = TRUE)) {
  install.packages("tidyr")
}

library(nanoparquet)
library(ggplot2)
library(dplyr)
library(tidyr)

# Parse command line arguments
args <- commandArgs(trailingOnly = TRUE)
input_file <- if(length(args) >= 1) args[1] else "collector-parquet.parquet"
start_time_offset <- if(length(args) >= 2) as.numeric(args[2]) else 110  # Default to 110 seconds after start
window_size <- if(length(args) >= 3) as.numeric(args[3]) else 1  # Default to 1 second window
output_file <- if(length(args) >= 4) args[4] else "llc_misses"
top_n_processes <- if(length(args) >= 5) as.numeric(args[5]) else 15  # Default to showing top processes

# Cache line size in bytes
CACHE_LINE_SIZE <- 64
# Bytes in a gigabyte (2^30)
BYTES_PER_GB <- 1073741824
# Nanoseconds in a second
NS_PER_SEC <- 1e9
# Nanoseconds in a millisecond
NS_PER_MS <- 1e6

# Function to load and process parquet data
load_and_process_parquet <- function(file_path, time_offset_pct, window_size_pct) {
  # Read the parquet file
  message("Reading parquet file: ", file_path)
  perf_data <- nanoparquet::read_parquet(file_path)
  
  # Convert start_time to nanoseconds since beginning of experiment
  # Normalize by subtracting the minimum start_time to get relative time
  # Keep integer precision by working with nanoseconds
  min_time <- min(perf_data$start_time, na.rm = TRUE)
  
  # Calculate window boundaries in nanoseconds
  # Convert parameters to numeric explicitly to prevent NA errors
  window_start_ns <- as.numeric(time_offset_pct) * NS_PER_SEC
  window_end_ns <- as.numeric(time_offset_pct + window_size_pct) * NS_PER_SEC
  
  # Calculate relative time in nanoseconds (preserving precision)
  perf_data$relative_time_ns <- perf_data$start_time - min_time
  
  message("Filtering data for time window: ", 
          window_start_ns / NS_PER_SEC, " to ", 
          window_end_ns / NS_PER_SEC, " seconds (absolute)")
  
  # Filter data within the window
  window_data <- perf_data %>%
    filter(relative_time_ns >= window_start_ns & 
           relative_time_ns <= window_end_ns)
  
  # Calculate millisecond timestamps within the window
  # Convert to 0-based milliseconds within the window
  window_data$ms_bucket <- as.integer((window_data$relative_time_ns - window_start_ns) / NS_PER_MS)
  
  # Replace NULL process names with "kernel" for better visualization
  window_data$process_name[is.na(window_data$process_name)] <- "kernel"
  
  # Ensure process_name is a factor with ordered levels based on total LLC misses
  process_totals <- window_data %>%
    group_by(process_name) %>%
    summarise(total_llc_misses = sum(llc_misses, na.rm = TRUE)) %>%
    arrange(desc(total_llc_misses))
  
  window_data$process_name <- factor(window_data$process_name, 
                                     levels = process_totals$process_name)
  
  # Store the time window information for plot titles (in seconds)
  attr(window_data, "window_start_s") <- window_start_ns / NS_PER_SEC
  attr(window_data, "window_end_s") <- window_end_ns / NS_PER_SEC
  
  return(window_data)
}

# Function to create the stacked area graph for LLC misses
create_llc_misses_plot <- function(data, n_top_processes = top_n_processes) {
  # Get time window info from attributes
  window_start_s <- attr(data, "window_start_s")
  window_end_s <- attr(data, "window_end_s")
  subtitle <- paste0("1-second window at ", start_time_offset, 
                    " seconds after experiment start (showing top ", n_top_processes, " processes)")
  
  # Calculate total LLC misses for validation
  total_llc_misses_original <- sum(data$llc_misses, na.rm = TRUE)
  
  # Select top processes by total LLC misses to keep the plot readable
  top_processes <- data %>%
    group_by(process_name) %>%
    summarise(total_llc_misses = sum(llc_misses, na.rm = TRUE)) %>%
    arrange(desc(total_llc_misses)) %>%
    slice_head(n = n_top_processes) %>%
    pull(process_name)
  
  top_processes_percent <- sum(data$llc_misses[data$process_name %in% top_processes], na.rm = TRUE) / 
          total_llc_misses_original * 100
          
  message("Top ", n_top_processes, " processes account for ",
          round(top_processes_percent, 2), "% of total LLC misses")
  
  # Filter data for top processes and group the rest as "other"
  plot_data <- data %>%
    mutate(process_group = ifelse(process_name %in% top_processes, 
                                 as.character(process_name), 
                                 "other"))
  
  # Aggregate by millisecond bucket and process group
  # Use integer ms_bucket to maintain precision and avoid floating point issues
  ms_data <- plot_data %>%
    group_by(ms_bucket, process_group) %>%
    summarise(llc_misses = sum(llc_misses, na.rm = TRUE), .groups = 'drop') %>%
    ungroup()

  # Set options to print all rows without truncation
  options(tibble.print_max = 100, tibble.width = Inf)
  # Print first 100 rows of ms_data sorted by ms_bucket
  message("First 100 rows of aggregated data:")
  print(ms_data %>% arrange(ms_bucket, process_group) %>% head(100))
  # Reset options to defaults
  options(tibble.print_max = 10)
  
  # Validate that no data was lost during aggregation
  total_llc_misses_after <- sum(ms_data$llc_misses, na.rm = TRUE)
  llc_diff <- abs(total_llc_misses_original - total_llc_misses_after)
  
  if (llc_diff > 0.01) {
    warning("Possible data loss in aggregation. Original total: ", 
            total_llc_misses_original, ", After aggregation: ", total_llc_misses_after,
            ", Difference: ", llc_diff)
  } else {
    message("Aggregation validation passed. All LLC misses accounted for.")
  }
  
  # Convert LLC misses to GB/s - since we have millisecond frames (1000 per second)
  # we need to multiply by 1000 to get the per-second rate
  ms_data <- ms_data %>%
    mutate(
      bytes = llc_misses * CACHE_LINE_SIZE,
      gb_per_second = (bytes / BYTES_PER_GB) * 1000  # Convert to GB/s (1000 millisecond frames per second)
    )
  
  # Calculate the total GB/s for sanity checking
  total_gb_per_second <- ms_data %>%
    group_by(ms_bucket) %>%
    summarise(total_gb_per_second = sum(gb_per_second, na.rm = TRUE)) %>%
    ungroup()
  
  median_total_gb <- median(total_gb_per_second$total_gb_per_second, na.rm = TRUE)
  max_total_gb <- max(total_gb_per_second$total_gb_per_second, na.rm = TRUE)
  
  message("Median total memory bandwidth: ", round(median_total_gb, 2), " GB/s")
  message("Maximum total memory bandwidth: ", round(max_total_gb, 2), " GB/s")
  
  # Generate colors for the plot
  all_colors <- colorRampPalette(
    c("#E41A1C", "#377EB8", "#4DAF4A", "#984EA3", "#FF7F00", 
      "#FFFF33", "#A65628", "#F781BF", "#999999")
  )(length(unique(ms_data$process_group)))
  
  names(all_colors) <- unique(ms_data$process_group)
  
  # If "other" is present, make it gray and place it at the bottom of the stack
  if ("other" %in% names(all_colors)) {
    all_colors["other"] <- "#CCCCCC"  # Gray for "other"
    
    # Reorder factor levels to ensure "other" is at the bottom of the stack
    # This gives most visibility to the actual named processes
    process_groups <- setdiff(names(all_colors), "other")
    ms_data$process_group <- factor(ms_data$process_group, 
                                  levels = c(process_groups, "other"))
  }
  
  # Create the stacked area plot
  p <- ggplot(ms_data, aes(x = ms_bucket, y = gb_per_second, fill = process_group)) +
    geom_col(position = "stack", width = 1.0, alpha = 0.8) +
    scale_fill_manual(values = all_colors) +
    scale_y_continuous(labels = function(x) sprintf("%.2f GB/s", x)) +
    labs(
      title = "Memory Bandwidth from LLC Misses by Process",
      subtitle = subtitle,
      x = "Time (milliseconds)",
      y = "Gigabytes Per Second",
      fill = "Process"
    ) +
    theme_minimal() +
    theme(
      legend.position = "right",
      panel.grid.minor = element_blank(),
      plot.title = element_text(face = "bold"),
      axis.title = element_text(face = "bold")
    )
  
  return(p)
}

# Main execution
main <- function() {
  tryCatch({
    # Check if input file exists
    if (!file.exists(input_file)) {
      stop("Input file does not exist: ", input_file)
    }
    
    message("Processing LLC misses data...")
    window_data <- load_and_process_parquet(input_file, start_time_offset, window_size)
    
    # Check if we have enough data
    if (nrow(window_data) < 10) {
      stop("Not enough data points in the selected time window. Try a different time offset or window size.")
    }
    
    message("Creating LLC misses plot...")
    llc_plot <- create_llc_misses_plot(window_data, top_n_processes)
    
    # Save outputs
    png_filename <- paste0(output_file, ".png")
    pdf_filename <- paste0(output_file, ".pdf")
    
    message("Saving output as PNG: ", png_filename)
    ggsave(png_filename, llc_plot, width = 10, height = 6, dpi = 300)
    
    message("Saving output as PDF: ", pdf_filename)
    ggsave(pdf_filename, llc_plot, width = 10, height = 6)
    
    message("Done!")
  }, error = function(e) {
    message("Error: ", e$message)
    quit(status = 1)
  })
}

# Execute main function
main() 