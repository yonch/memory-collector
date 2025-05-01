#!/usr/bin/env Rscript

# plot_workload_performance.R
#
# Script to visualize Locust load generator performance metrics
# Focuses on the "Aggregated" data lines to show RPS and latency metrics
# over the course of an experiment.
#
# Usage:
#   Rscript plot_workload_performance.R <stats_history_file> [output_file]
#
# Where:
#   <stats_history_file>: Path to the Locust stats history CSV file
#   [output_file]: Base name for output files (default: "workload_performance")

# Load required libraries
suppressPackageStartupMessages({
  library(ggplot2)
  library(dplyr)
  library(readr)
  library(tidyr)
  library(scales)
})

# Parse command line arguments
args <- commandArgs(trailingOnly = TRUE)
input_file <- if (length(args) >= 1) args[1] else "stats_stats_history.csv"
output_file <- if (length(args) >= 2) args[2] else "workload_performance"

# Display information about the analysis
cat(sprintf("Analyzing Locust metrics from: %s\n", input_file))
cat(sprintf("Output will be saved as: %s.[png/pdf]\n", output_file))

# Read and preprocess the data
process_data <- function(file_path) {
  # Read CSV file with proper handling for "N/A" values
  data <- read_csv(file_path, show_col_types = FALSE, na = c("", "NA", "N/A"))
  
  # Filter for only "Aggregated" rows
  aggregated_data <- data %>%
    filter(Name == "Aggregated")
  
  # Convert timestamp to relative time (seconds from start)
  if (nrow(aggregated_data) > 0) {
    start_time <- min(aggregated_data$Timestamp, na.rm = TRUE)
    aggregated_data$RelativeTime <- aggregated_data$Timestamp - start_time
    
    # Convert percentage columns to numeric, replacing NA with 0
    percentage_cols <- c("50%", "66%", "75%", "80%", "90%", "95%", "98%", "99%", "99.9%", "99.99%", "100%")
    for (col in percentage_cols) {
      aggregated_data[[col]] <- as.numeric(aggregated_data[[col]])
      aggregated_data[[col]][is.na(aggregated_data[[col]])] <- 0
    }
  }
  
  return(aggregated_data)
}

# Create the multi-axis visualization
create_workload_plot <- function(data) {
  # Check if we have data to plot
  if (nrow(data) == 0) {
    stop("No 'Aggregated' data found in the input file")
  }
  
  # Calculate max values for scaling with buffer
  max_rps <- max(data$`Requests/s`, na.rm = TRUE)
  max_rps_with_buffer <- max_rps * 1.2  # Add 20% buffer for better visualization
  
  # Calculate max latency with buffer
  max_latency <- max(c(data$`50%`, data$`95%`, data$`99%`), na.rm = TRUE)
  max_latency_with_buffer <- max_latency * 1.2  # Add 20% buffer
  
  # Scale factor for latency to RPS conversion (for dual axis)
  # We want latency_value * scale_factor to be in the RPS range
  scale_factor <- max_rps_with_buffer / max_latency_with_buffer
  
  # Create the plot with dual y-axes
  p <- ggplot(data, aes(x = RelativeTime)) +
    # RPS line (primary y-axis)
    geom_line(aes(y = `Requests/s`, color = "RPS"), linewidth = 1) +
    # Latency lines (secondary y-axis, scaled)
    geom_line(aes(y = `50%` * scale_factor, color = "Median (P50)"), linewidth = 0.8) +
    geom_line(aes(y = `95%` * scale_factor, color = "P95"), linewidth = 0.8) +
    geom_line(aes(y = `99%` * scale_factor, color = "P99"), linewidth = 0.8) +
    # Primary axis (RPS)
    scale_y_continuous(
      name = "Requests per Second",
      limits = c(0, max_rps_with_buffer),
      sec.axis = sec_axis(~./scale_factor, name = "Latency (ms)", 
                          breaks = seq(0, max_latency_with_buffer, 
                                       length.out = 6))
    ) +
    # X-axis label
    scale_x_continuous(name = "Time (seconds)") +
    # Custom colors
    scale_color_manual(
      name = "Metrics",
      values = c(
        "RPS" = "#1F77B4",
        "Median (P50)" = "#2CA02C",
        "P95" = "#FF7F0E",
        "P99" = "#D62728"
      )
    ) +
    # Theme and labels
    labs(
      title = "Workload Performance Over Time",
      subtitle = "Comparing RPS and Latency Percentiles"
    ) +
    theme_minimal() +
    theme(
      legend.position = "bottom",
      legend.title = element_text(size = 10),
      legend.text = element_text(size = 9),
      plot.title = element_text(size = 14, face = "bold"),
      plot.subtitle = element_text(size = 12),
      axis.title = element_text(size = 11),
      axis.text = element_text(size = 9),
      panel.grid.minor = element_line(color = "grey90"),
      panel.grid.major = element_line(color = "grey85")
    )
  
  return(p)
}

# Create the RPS vs User Count visualization
create_rps_users_plot <- function(data) {
  # Check if we have data to plot
  if (nrow(data) == 0) {
    stop("No 'Aggregated' data found in the input file")
  }
  
  # Use only data points with users > 0
  plot_data <- data %>% filter(`User Count` > 0)
  
  # Create the scatter plot with smoothed line
  p <- ggplot(plot_data, aes(x = `User Count`, y = `Requests/s`)) +
    geom_point(alpha = 0.7, size = 2, color = "#1F77B4") +
    geom_smooth(method = "loess", se = TRUE, color = "#FF7F0E", fill = "#FF7F0E20") +
    scale_x_continuous(name = "Concurrent Users") +
    scale_y_continuous(name = "Requests per Second") +
    labs(
      title = "Workload Scaling Characteristics",
      subtitle = "RPS vs Concurrent Users"
    ) +
    theme_minimal() +
    theme(
      plot.title = element_text(size = 14, face = "bold"),
      plot.subtitle = element_text(size = 12),
      axis.title = element_text(size = 11),
      axis.text = element_text(size = 9),
      panel.grid.minor = element_line(color = "grey90"),
      panel.grid.major = element_line(color = "grey85")
    )
  
  return(p)
}

# Create the latency percentiles visualization
create_latency_plot <- function(data) {
  # Check if we have data to plot
  if (nrow(data) == 0) {
    stop("No 'Aggregated' data found in the input file")
  }
  
  # Filter out rows with all-zero latencies
  plot_data <- data %>%
    filter(`50%` > 0 | `95%` > 0 | `99%` > 0)
  
  # If no valid data remains, return an empty plot with message
  if (nrow(plot_data) == 0) {
    p <- ggplot() +
      annotate("text", x = 0.5, y = 0.5, label = "No latency data available") +
      theme_void() +
      theme(
        plot.title = element_text(size = 14, face = "bold"),
        plot.subtitle = element_text(size = 12)
      ) +
      labs(
        title = "Response Time Percentiles Over Time",
        subtitle = "No valid latency data found"
      )
    return(p)
  }
  
  # Reshape the data for latency percentiles
  latency_data <- plot_data %>%
    select(RelativeTime, `User Count`, `50%`, `95%`, `99%`) %>%
    pivot_longer(
      cols = c(`50%`, `95%`, `99%`),
      names_to = "Percentile",
      values_to = "Latency"
    )
  
  # Create the plot
  p <- ggplot(latency_data, aes(x = RelativeTime, y = Latency, color = Percentile)) +
    geom_line(linewidth = 0.8) +
    scale_color_manual(
      values = c(
        "50%" = "#2CA02C",
        "95%" = "#FF7F0E",
        "99%" = "#D62728"
      ),
      labels = c("Median (P50)", "P95", "P99")
    ) +
    scale_x_continuous(name = "Time (seconds)") +
    scale_y_continuous(name = "Latency (ms)") +
    labs(
      title = "Response Time Percentiles Over Time",
      subtitle = "P50, P95, and P99 Latencies"
    ) +
    theme_minimal() +
    theme(
      legend.position = "bottom",
      legend.title = element_blank(),
      plot.title = element_text(size = 14, face = "bold"),
      plot.subtitle = element_text(size = 12),
      axis.title = element_text(size = 11),
      axis.text = element_text(size = 9),
      panel.grid.minor = element_line(color = "grey90"),
      panel.grid.major = element_line(color = "grey85")
    )
  
  return(p)
}

# Generate summary statistics
generate_summary <- function(data) {
  # Calculate summary statistics, handling potential NA values
  summary <- data %>%
    summarise(
      max_users = max(`User Count`, na.rm = TRUE),
      max_rps = max(`Requests/s`, na.rm = TRUE),
      avg_rps = mean(`Requests/s`, na.rm = TRUE),
      max_p95_latency = max(`95%`, na.rm = TRUE),
      avg_p95_latency = mean(`95%`, na.rm = TRUE),
      max_p99_latency = max(`99%`, na.rm = TRUE),
      avg_p99_latency = mean(`99%`, na.rm = TRUE)
    )
  
  # Print summary
  cat("\nWorkload Performance Summary:\n")
  cat("----------------------------\n")
  cat(sprintf("Maximum Users: %d\n", summary$max_users))
  cat(sprintf("Maximum RPS: %.2f\n", summary$max_rps))
  cat(sprintf("Average RPS: %.2f\n", summary$avg_rps))
  cat(sprintf("Maximum P95 Latency: %.2f ms\n", summary$max_p95_latency))
  cat(sprintf("Average P95 Latency: %.2f ms\n", summary$avg_p95_latency))
  cat(sprintf("Maximum P99 Latency: %.2f ms\n", summary$max_p99_latency))
  cat(sprintf("Average P99 Latency: %.2f ms\n", summary$avg_p99_latency))
  
  return(summary)
}

# Main execution
tryCatch({
  # Process data
  cat("Processing data...\n")
  data <- process_data(input_file)
  
  # Generate and save plots
  cat("Generating plots...\n")
  
  # Plot 1: Combined RPS and Latency
  p1 <- create_workload_plot(data)
  ggsave(paste0(output_file, ".png"), p1, width = 10, height = 6, dpi = 300)
  ggsave(paste0(output_file, ".pdf"), p1, width = 10, height = 6)
  
  # Plot 2: RPS vs User Count (only if we have data with users > 0)
  if (any(data$`User Count` > 0)) {
    p2 <- create_rps_users_plot(data)
    ggsave(paste0(output_file, "_scaling.png"), p2, width = 8, height = 6, dpi = 300)
    ggsave(paste0(output_file, "_scaling.pdf"), p2, width = 8, height = 6)
  } else {
    cat("Skipping scaling plot - no data with users > 0\n")
  }
  
  # Plot 3: Latency percentiles (only if we have latency data)
  if (any(data$`50%` > 0 | data$`95%` > 0 | data$`99%` > 0)) {
    p3 <- create_latency_plot(data)
    ggsave(paste0(output_file, "_latency.png"), p3, width = 8, height = 6, dpi = 300)
    ggsave(paste0(output_file, "_latency.pdf"), p3, width = 8, height = 6)
  } else {
    cat("Skipping latency plot - no valid latency data\n")
  }
  
  # Generate summary statistics
  summary <- generate_summary(data)
  
  cat("\nPlots saved successfully!\n")
  cat(sprintf("- %s.png/pdf: Combined RPS and latency visualization\n", output_file))
  if (any(data$`User Count` > 0)) {
    cat(sprintf("- %s_scaling.png/pdf: RPS vs User Count visualization\n", output_file))
  }
  if (any(data$`50%` > 0 | data$`95%` > 0 | data$`99%` > 0)) {
    cat(sprintf("- %s_latency.png/pdf: Latency percentiles visualization\n", output_file))
  }
  
}, error = function(e) {
  cat(sprintf("Error: %s\n", e$message))
  quit(status = 1)
}) 