#!/usr/bin/env Rscript

# Load required libraries
library(ggplot2)
library(dplyr)
library(readr)
library(tidyr)

# Function to parse the memory metrics file
parse_memory_metrics <- function(file_path) {
  # Read the entire file as text with warning suppression for incomplete final line
  lines <- suppressWarnings(readLines(file_path))
  
  # Remove last line if it's malformed (incomplete)
  last_line <- lines[length(lines)]
  if (nchar(last_line) < 10 || !grepl(";", last_line)) {
    cat("Removing malformed last line:", last_line, "\n")
    lines <- lines[1:(length(lines)-1)]
  }
  
  # Filter out header lines and system info lines
  data_lines <- lines[!grepl("^Linux|^[0-9]+;UID", lines)]
  
  # Create a dataframe from the filtered lines
  df <- data.frame(line = data_lines, stringsAsFactors = FALSE)
  
  # Parse the data into columns with warning suppression
  df <- suppressWarnings(
    df %>%
      separate(line, into = c("timestamp", "uid", "pid", "minflt", "majflt", 
                            "vsz", "rss", "mem_percent", "command"), 
              sep = ";", convert = TRUE, fill = "right")
  )
  
  # Remove rows with NA in essential columns
  df <- df %>% 
    filter(!is.na(timestamp) & !is.na(pid) & !is.na(rss) & !is.na(command))
  
  # Convert timestamp to relative seconds from start
  min_timestamp <- min(df$timestamp, na.rm = TRUE)
  df$relative_time <- df$timestamp - min_timestamp
  
  # Convert RSS from KB to MB
  df$rss_mb <- df$rss / 1024
  
  return(df)
}

# Function to plot memory utilization
plot_memory_utilization <- function(df, process_name, output_file = NULL) {
  # Filter data for the specific process
  process_data <- df %>%
    filter(grepl(process_name, command, ignore.case = TRUE))
  
  if (nrow(process_data) == 0) {
    stop(paste("No data found for process:", process_name))
  }
  
  # Create appropriate plot based on number of data points
  if (nrow(process_data) == 1) {
    # For a single data point, create a point plot
    p <- ggplot(process_data, aes(x = relative_time, y = rss_mb)) +
      geom_point(size = 3, color = "blue") +
      labs(
        title = paste("Memory Utilization of", process_name, "Process"),
        subtitle = "Note: Only one data point available",
        x = "Time (seconds)",
        y = "Memory Usage (MB)",
        caption = "Source: pidstat output"
      ) +
      theme_minimal() +
      theme(
        plot.title = element_text(hjust = 0.5),
        plot.subtitle = element_text(hjust = 0.5),
        legend.position = "none"
      )
  } else {
    # For multiple data points, create a line plot
    p <- ggplot(process_data, aes(x = relative_time, y = rss_mb)) +
      geom_line() +
      geom_point() +
      labs(
        title = paste("Memory Utilization of", process_name, "Process"),
        x = "Time (seconds)",
        y = "Memory Usage (MB)",
        caption = "Source: pidstat output"
      ) +
      theme_minimal() +
      theme(
        plot.title = element_text(hjust = 0.5),
        legend.position = "none"
      )
  }
  
  # Save the plot if output file is specified
  if (!is.null(output_file)) {
    ggsave(paste0(output_file, ".png"), p, width = 10, height = 6)
    ggsave(paste0(output_file, ".pdf"), p, width = 10, height = 6)
    cat("Plot saved to", paste0(output_file, ".png"), "and", paste0(output_file, ".pdf"), "\n")
  }
  
  return(p)
}

# Main execution
main <- function() {
  args <- commandArgs(trailingOnly = TRUE)
  
  if (length(args) < 1) {
    cat("Usage: Rscript plot_memory_utilization.R <memory_metrics_file> [process_name] [output_file]\n")
    cat("Example: Rscript plot_memory_utilization.R memory_metrics.csv collector memory_plot\n")
    quit(status = 1)
  }
  
  file_path <- args[1]
  process_name <- if (length(args) >= 2) args[2] else "collector"
  output_file <- if (length(args) >= 3) args[3] else "memory_utilization"
  
  # Parse the data
  cat("Parsing memory metrics from", file_path, "...\n")
  memory_data <- parse_memory_metrics(file_path)
  
  # Print summary of parsed data
  cat("Parsed", nrow(memory_data), "data points from", file_path, "\n")
  
  # Plot the data
  cat("Generating plot for process:", process_name, "...\n")
  plot <- plot_memory_utilization(memory_data, process_name, output_file)
  
  # Print summary statistics
  process_data <- memory_data %>%
    filter(grepl(process_name, command, ignore.case = TRUE))
  
  if (nrow(process_data) > 0) {
    cat("\nSummary statistics for", process_name, "process:\n")
    cat("Number of data points:", nrow(process_data), "\n")
    cat("Min memory usage:", min(process_data$rss_mb, na.rm = TRUE), "MB\n")
    cat("Max memory usage:", max(process_data$rss_mb, na.rm = TRUE), "MB\n")
    cat("Average memory usage:", mean(process_data$rss_mb, na.rm = TRUE), "MB\n")
  }
}

# Run the main function
if (!interactive()) {
  main()
} 