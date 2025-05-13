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

# Parse command line arguments
args <- commandArgs(trailingOnly = TRUE)
input_file <- if(length(args) >= 1) args[1] else "collector-parquet.parquet"
instruction_threshold <- if(length(args) >= 2) as.numeric(args[2]) else 100000  # Default to 100k instructions
output_file <- if(length(args) >= 3) args[3] else "cpi_by_llc_misses"
top_n_processes <- if(length(args) >= 4) as.numeric(args[4]) else 23  # Default to showing top 23 processes + "other"
llc_percentile <- if(length(args) >= 5) as.numeric(args[5]) else 75  # Default to 75th percentile

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
  
  return(perf_data)
}

# Function to analyze LLC misses and create the faceted histogram
create_cpi_llc_analysis <- function(data, instruction_threshold, top_n_processes, llc_percentile) {
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
    geom_histogram(position = "identity", alpha = 0.7, bins = 30) +
    facet_wrap(~ process_group, scales = "free_y", ncol = 3) +
    scale_fill_manual(values = c("#E41A1C", "#377EB8")) +
    labs(
      title = "Cycles Per Instruction (CPI) Distribution by Process",
      subtitle = paste0("Comparing high vs. low LLC miss periods (threshold: ", 
                       instruction_threshold, " instructions, ", llc_percentile, "% LLC miss percentile)"),
      x = "Cycles Per Instruction (CPI)",
      y = "Count",
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
    
    message("Creating CPI by LLC misses plot...")
    cpi_plot <- create_cpi_llc_analysis(perf_data, instruction_threshold, top_n_processes, llc_percentile)
    
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