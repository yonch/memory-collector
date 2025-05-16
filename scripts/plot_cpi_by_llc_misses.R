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
if (!requireNamespace("forcats", quietly = TRUE)) {
  install.packages("forcats")
}

library(nanoparquet)
library(ggplot2)
library(dplyr)
library(tidyr)
library(forcats)

# Script Description:
# This script analyzes performance data from a parquet file and creates CPI distribution
# plots comparing high vs. low LLC miss periods for different processes.
#
# Command line arguments:
# 1. input_file: Path to the input parquet file (default: collector-parquet.parquet)
# 2. instruction_threshold: Minimum number of instructions for a sample to be considered (default: 100000)
# 3. output_file: Base name for output files without extension (default: cpi_by_llc_misses)
# 4. top_n_processes: Number of top processes to show individually (default: 23)
# 5. llc_percentile: Percentile threshold for high LLC misses (default: 75)
# 6. start_time_seconds: Start time in seconds for the analysis window (default: 205)
# 7. end_time_seconds: End time in seconds for the analysis window (default: 255)
#
# Example usage:
# Rscript plot_cpi_by_llc_misses.R my-data.parquet 50000 my-output 15 80 200 300
# This would use my-data.parquet, 50k instruction threshold, output to my-output.{png,pdf},
# show top 15 processes, use 80th percentile LLC threshold, and analyze data from 200-300 seconds.

# Parse command line arguments
args <- commandArgs(trailingOnly = TRUE)
input_file <- if(length(args) >= 1) args[1] else "collector-parquet.parquet"
instruction_threshold <- if(length(args) >= 2) as.numeric(args[2]) else 100000  # Default to 100k instructions
output_file <- if(length(args) >= 3) args[3] else "cpi_by_llc_misses"
top_n_processes <- if(length(args) >= 4) as.numeric(args[4]) else 23  # Default to showing top 23 processes + "other"
llc_percentile <- if(length(args) >= 5) as.numeric(args[5]) else 75  # Default to 75th percentile
start_time_seconds <- if(length(args) >= 6) as.numeric(args[6]) else 205  # Default steady state starts at 205 seconds
end_time_seconds <- if(length(args) >= 7) as.numeric(args[7]) else 255  # Default steady state ends at 255 seconds

# Function to load and process parquet data
load_and_process_parquet <- function(file_path) {
  # Read the parquet file
  message("Reading parquet file: ", file_path)
  perf_data <- nanoparquet::read_parquet(file_path)
  
  # Replace NULL process names with "kernel" for better visualization
  perf_data$process_name[is.na(perf_data$process_name)] <- "kernel"
  
  # Extract millisecond time slots from start_time (nanoseconds)
  # We're assuming the timestamps are already aligned to millisecond boundaries
  perf_data$ms_slot <- floor(perf_data$start_time / 1e6)
  
  # Calculate cycles per instruction (CPI) for each sample
  perf_data$cpi <- perf_data$cycles / pmax(perf_data$instructions, 1)  # Avoid division by zero
  
  # Convert start_time from nanoseconds to seconds for time window filtering
  perf_data$time_seconds <- perf_data$start_time / 1e9
  
  return(perf_data)
}

# Function to analyze LLC misses and create the faceted histogram
create_cpi_llc_analysis <- function(data, instruction_threshold, top_n_processes, llc_percentile, 
                                   start_time_seconds, end_time_seconds) {
  message("Analyzing LLC misses and CPI...")
  
  # Calculate aggregate LLC misses for each millisecond time slot
  ms_aggregates <- data %>%
    group_by(ms_slot) %>%
    summarise(
      total_llc_misses = sum(llc_misses, na.rm = TRUE),
      .groups = "drop"
    )
  
  # Determine the LLC misses threshold at the specified percentile
  llc_threshold <- quantile(ms_aggregates$total_llc_misses, llc_percentile/100, na.rm = TRUE)
  message(llc_percentile, "th percentile of LLC misses per millisecond: ", llc_threshold)

  # Create category labels for the legend
  high_label <- paste0("High LLC Misses (>", llc_percentile, "%)")
  low_label <- paste0("Low LLC Misses (<", llc_percentile, "%)")

  # Classify time slots as high or low LLC miss periods
  ms_aggregates <- ms_aggregates %>%
    mutate(llc_category = ifelse(total_llc_misses > llc_threshold, 
                               high_label, 
                               low_label))
  
  # Join the LLC category back to the original data
  data_with_llc <- data %>%
    left_join(ms_aggregates, by = "ms_slot")
  
  # Identify top processes by total instructions
  top_processes <- data %>%
    group_by(process_name) %>%
    summarise(total_instructions = sum(instructions, na.rm = TRUE)) %>%
    arrange(desc(total_instructions)) %>%
    slice_head(n = top_n_processes) %>%
    pull(process_name)
  
  message("Top ", length(top_processes), " processes by instructions:")
  print(head(top_processes, 10))
  
  # Filter for significant samples (>instruction_threshold) and prepare for plotting
  plot_data <- data_with_llc %>%
    # Group all non-top processes as "other"
    mutate(process_group = ifelse(process_name %in% top_processes, 
                                 as.character(process_name), 
                                 "other")) %>%
    # Filter for samples with significant instruction counts
    filter(instructions > instruction_threshold) %>%
    # Cap extreme CPI values for better visualization
    mutate(cpi_capped = pmin(cpi, 10))  # Cap at 10 cycles per instruction
  
  # Count samples per process for reporting
  process_counts <- plot_data %>%
    group_by(process_group, llc_category) %>%
    summarise(
      sample_count = n(),
      avg_cpi = mean(cpi, na.rm = TRUE),
      .groups = "drop"
    )
  
  message("Sample counts and average CPI by process and LLC category:")
  print(process_counts)
  
  # Ensure we have enough data to plot
  if(nrow(plot_data) < 10) {
    stop("Not enough data points after filtering. Try lowering the instruction threshold.")
  }
  
  # Reorder factor levels based on total instructions for better visualization
  process_order <- c(top_processes, "other")
  plot_data$process_group <- factor(plot_data$process_group, levels = process_order)
  
  
  # Create the faceted histogram plot
  p <- ggplot(plot_data, aes(x = cpi_capped, fill = llc_category)) +
    geom_density(alpha = 0.7, adjust = 1.5) +
    facet_wrap(~ process_group, scales = "free_y", ncol = 3) +
    scale_fill_manual(values = c("#E41A1C", "#377EB8")) +
    labs(
      title = "Cycles Per Instruction (CPI) Distribution by Process",
      subtitle = paste0("Comparing high vs. low LLC miss periods (", 
                       start_time_seconds, "-", end_time_seconds, "s window, ",
                       instruction_threshold, " instructions, ", llc_percentile, "% LLC miss percentile)"),
      x = "Cycles Per Instruction (CPI)",
      y = "Density",
      fill = "LLC Miss Category"
    ) +
    theme_minimal() +
    theme(
      legend.position = "top",
      strip.background = element_rect(fill = "lightgray"),
      strip.text = element_text(face = "bold"),
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
    
    message("Processing performance data...")
    perf_data <- load_and_process_parquet(input_file)
    
    # Check if we have enough data
    if (nrow(perf_data) < 10) {
      stop("Not enough data points in the input file.")
    }
    
    # Apply time window filtering
    message(sprintf("Filtering data for time window: %.1f - %.1f seconds", start_time_seconds, end_time_seconds))
    perf_data_filtered <- perf_data %>%
      filter(time_seconds >= start_time_seconds & time_seconds <= end_time_seconds)
    
    # Check if we still have enough data after time filtering
    if (nrow(perf_data_filtered) < 10) {
      stop("Not enough data points after time window filtering. Consider adjusting the time window.")
    }
    
    message(sprintf("Retained %d of %d samples (%.1f%%) after time window filtering", 
                   nrow(perf_data_filtered), nrow(perf_data), 
                   100 * nrow(perf_data_filtered) / nrow(perf_data)))
    
    message("Creating CPI by LLC misses plot...")
    cpi_plot <- create_cpi_llc_analysis(perf_data_filtered, instruction_threshold, top_n_processes, llc_percentile, 
                                         start_time_seconds, end_time_seconds)
    
    # Save outputs
    png_filename <- paste0(output_file, ".png")
    pdf_filename <- paste0(output_file, ".pdf")
    
    message("Saving output as PNG: ", png_filename)
    ggsave(png_filename, cpi_plot, width = 15, height = 20, dpi = 300)
    
    message("Saving output as PDF: ", pdf_filename)
    ggsave(pdf_filename, cpi_plot, width = 15, height = 20)
    
    message("Done!")
  }, error = function(e) {
    message("Error: ", e$message)
    quit(status = 1)
  })
}

# Execute main function
main()