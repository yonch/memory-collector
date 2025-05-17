#!/usr/bin/env Rscript

# plot_workload_performance.R
#
# Script to visualize Locust load generator performance metrics
# Creates faceted plots for each API endpoint (Type+Name combination)
# Shows RPS, failures per second, and latency metrics over time
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
  library(gridExtra)
  library(stringr)
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
  
  # Filter out rows with empty Type or Name
  data <- data %>%
    filter(!is.na(Type) & Type != "" & !is.na(Name) & Name != "")
  
  # Create endpoint identifier (Type + Name)
  data <- data %>%
    mutate(Endpoint = paste(Type, Name, sep = " "))
  
  # Convert timestamp to relative time (seconds from start)
  if (nrow(data) > 0) {
    start_time <- min(data$Timestamp, na.rm = TRUE)
    data$RelativeTime <- data$Timestamp - start_time
    
    # Convert percentage columns to numeric, replacing NA with 0
    percentage_cols <- c("50%", "66%", "75%", "80%", "90%", "95%", "98%", "99%", "99.9%", "99.99%", "100%")
    for (col in percentage_cols) {
      data[[col]] <- as.numeric(data[[col]])
      data[[col]][is.na(data[[col]])] <- 0
    }
  }
  
  return(data)
}

# Create the multi-axis visualization for each endpoint
create_endpoint_plot <- function(data) {
  # Check if we have data to plot
  if (nrow(data) == 0) {
    stop("No data found in the input file")
  }
  
  # Filter out specific product IDs and keep only aggregate endpoints
  # For example, keep "/api/products/[id]" but filter out "/api/products/1YMWWN1N4O", etc.
  filtered_data <- data %>%
    filter(
      # Keep if NOT a specific product ID pattern
      !(grepl("^/api/products/[A-Z0-9]{10}$", Name) | 
        grepl("^GET /api/products/[A-Z0-9]{10}$", Endpoint) |
        grepl("^POST /api/products/[A-Z0-9]{10}$", Endpoint))
    )
  
  # Get unique endpoints that are not "Aggregated"
  endpoints <- filtered_data %>%
    filter(Name != "Aggregated") %>%
    select(Endpoint) %>%
    distinct() %>%
    pull(Endpoint)
  
  # If no endpoints found, inform the user
  if (length(endpoints) == 0) {
    cat("No specific endpoints found in the data, only aggregated rows.\n")
    return(NULL)
  }
  
  # Filter data for endpoints only
  endpoints_data <- filtered_data %>% 
    filter(Endpoint %in% endpoints)

  # Define color palette for consistency
  color_palette <- c(
    "RPS" = "#1F77B4",
    "Failures/s" = "#E31A1C", 
    "Median (P50)" = "#2CA02C",
    "P95" = "#FF7F0E",
    "P99" = "#D62728"
  )

  # Function to extract the legend from a plot
  get_legend <- function(p) {
    tmp <- ggplot_gtable(ggplot_build(p))
    leg <- which(sapply(tmp$grobs, function(x) x$name) == "guide-box")
    legend <- tmp$grobs[[leg]]
    return(legend)
  }

  # Define our plotting function to create each facet with proper scaling
  endpoint_plot <- function() {
    # Create a list to hold each facet plot
    plots <- list()
    
    # Create a plot for each endpoint
    for (ep in endpoints) {
      # Filter data for this specific endpoint
      ep_data <- endpoints_data %>% filter(Endpoint == ep)
            
      # Create a local function that captures the current scale_factor
      create_plot <- function(ep_data, ep) {
        # Calculate scale factors for this endpoint
        max_rate <- max(c(ep_data$`Requests/s`, ep_data$`Failures/s`), na.rm = TRUE)
        max_latency <- max(c(ep_data$`50%`, ep_data$`95%`, ep_data$`99%`), na.rm = TRUE)
        
        # Avoid division by zero
        scale_factor <- if (max_latency > 0) max_rate / max_latency else 1

        cat(sprintf("Endpoint: %s, Max Rate: %.2f, Max Latency: %.2f, Scale Factor: %.2f\n", 
                    ep, max_rate, max_latency, scale_factor))

        ggplot(ep_data, aes(x = RelativeTime)) +
          # RPS and Failures lines (primary y-axis)
          geom_line(aes(y = `Requests/s`, color = "RPS"), linewidth = 1.2) +
          geom_line(aes(y = `Failures/s`, color = "Failures/s"), linewidth = 1.2, linetype = "dashed") +
          
          # Latency lines (scaled to primary y-axis)
          geom_line(aes(y = `50%` * scale_factor, color = "Median (P50)"), linewidth = 1.0) +
          geom_line(aes(y = `95%` * scale_factor, color = "P95"), linewidth = 1.0) +
          geom_line(aes(y = `99%` * scale_factor, color = "P99"), linewidth = 1.0) +
          
          # Axes
          scale_y_continuous(
            name = "Requests / Failures per Second",
            sec.axis = sec_axis(~./scale_factor, name = "Latency (ms)")
          ) +
          scale_x_continuous(name = "Time (seconds)") +
          
          # Custom colors
          scale_color_manual(
            name = "Metrics",
            values = color_palette
          ) +
          
          # Title and theme
          ggtitle(ep) +
          theme_minimal() +
          theme(
            legend.position = "none",  # Remove individual legends
            plot.title = element_text(size = 12, face = "bold"),
            axis.title = element_text(size = 9),
            axis.text = element_text(size = 8),
            panel.grid.minor = element_line(color = "grey90"),
            panel.grid.major = element_line(color = "grey85")
          )
      }
      
      # Create the plot with the captured scale_factor
      plots[[ep]] <- create_plot(ep_data, ep)
    }
    
    return(plots)
  }
  
  # Create a dummy plot to extract the legend
  legend_plot <- ggplot(endpoints_data, aes(x = RelativeTime)) +
    geom_line(aes(y = `Requests/s`, color = "RPS")) +
    geom_line(aes(y = `Failures/s`, color = "Failures/s"), linetype = "dashed") +
    geom_line(aes(y = `50%`, color = "Median (P50)")) +
    geom_line(aes(y = `95%`, color = "P95")) +
    geom_line(aes(y = `99%`, color = "P99")) +
    scale_color_manual(name = "Metrics", values = color_palette) +
    theme(legend.position = "bottom")
  
  # Extract the legend
  legend <- get_legend(legend_plot)
  
  # Generate all plots
  endpoint_plots <- endpoint_plot()
  
  # Arrange them in a grid
  if (length(endpoint_plots) > 0) {
    # Calculate layout based on number of plots
    n_plots <- length(endpoint_plots)
    n_cols <- min(3, n_plots)
    n_rows <- ceiling(n_plots / n_cols)
    
    # Create a title and subtitle
    title <- grid::textGrob(
      "API Endpoint Performance Over Time", 
      gp = grid::gpar(fontface = "bold", fontsize = 14)
    )
    
    subtitle <- grid::textGrob(
      "Requests/s, Failures/s, and Latency by Endpoint",
      gp = grid::gpar(fontsize = 12)
    )
    
    # Arrange plots in a grid with a shared legend at the bottom
    combined_plot <- gridExtra::grid.arrange(
      gridExtra::arrangeGrob(
        grobs = endpoint_plots,
        ncol = n_cols,
        nrow = n_rows,
        top = title
      ),
      legend,
      heights = c(20, 1),
      ncol = 1
    )
    
    return(combined_plot)
  } else {
    return(NULL)
  }
}

# Generate summary statistics
generate_summary <- function(data) {
  # Filter out specific product IDs and keep only aggregate endpoints
  filtered_data <- data %>%
    filter(
      # Keep if NOT a specific product ID pattern
      !(grepl("^/api/products/[A-Z0-9]{10}$", Name) | 
        grepl("^GET /api/products/[A-Z0-9]{10}$", Endpoint) |
        grepl("^POST /api/products/[A-Z0-9]{10}$", Endpoint))
    )
  
  # Overall summary from aggregated data
  agg_data <- filtered_data %>%
    filter(Name == "Aggregated")
  
  if (nrow(agg_data) > 0) {
    summary <- agg_data %>%
      summarise(
        max_users = max(`User Count`, na.rm = TRUE),
        max_rps = max(`Requests/s`, na.rm = TRUE),
        avg_rps = mean(`Requests/s`, na.rm = TRUE),
        max_failures = max(`Failures/s`, na.rm = TRUE),
        avg_failures = mean(`Failures/s`, na.rm = TRUE),
        max_p95_latency = max(`95%`, na.rm = TRUE),
        avg_p95_latency = mean(`95%`, na.rm = TRUE),
        max_p99_latency = max(`99%`, na.rm = TRUE),
        avg_p99_latency = mean(`99%`, na.rm = TRUE)
      )
    
    # Print overall summary
    cat("\nOverall Workload Performance Summary:\n")
    cat("------------------------------------\n")
    cat(sprintf("Maximum Users: %d\n", summary$max_users))
    cat(sprintf("Maximum RPS: %.2f\n", summary$max_rps))
    cat(sprintf("Average RPS: %.2f\n", summary$avg_rps))
    cat(sprintf("Maximum Failures/s: %.2f\n", summary$max_failures))
    cat(sprintf("Average Failures/s: %.2f\n", summary$avg_failures))
    cat(sprintf("Maximum P95 Latency: %.2f ms\n", summary$max_p95_latency))
    cat(sprintf("Average P95 Latency: %.2f ms\n", summary$avg_p95_latency))
    cat(sprintf("Maximum P99 Latency: %.2f ms\n", summary$max_p99_latency))
    cat(sprintf("Average P99 Latency: %.2f ms\n", summary$avg_p99_latency))
  } else {
    cat("\nNo aggregated data available for overall summary\n")
  }
  
  # Endpoint-specific summary
  endpoint_data <- filtered_data %>%
    filter(Name != "Aggregated") %>%
    group_by(Endpoint) %>%
    summarise(
      max_rps = max(`Requests/s`, na.rm = TRUE),
      avg_rps = mean(`Requests/s`, na.rm = TRUE),
      max_failures = max(`Failures/s`, na.rm = TRUE),
      total_failures = sum(`Total Failure Count`, na.rm = TRUE),
      max_p95_latency = max(`95%`, na.rm = TRUE),
      avg_latency = mean(`Total Average Response Time`, na.rm = TRUE),
      .groups = 'drop'
    ) %>%
    arrange(desc(max_rps))
  
  if (nrow(endpoint_data) > 0) {
    cat("\nEndpoint Performance Summary:\n")
    cat("---------------------------\n")
    for (i in 1:nrow(endpoint_data)) {
      endpoint <- endpoint_data$Endpoint[i]
      cat(sprintf("Endpoint: %s\n", endpoint))
      cat(sprintf("  Max RPS: %.2f, Avg RPS: %.2f\n", 
                 endpoint_data$max_rps[i], endpoint_data$avg_rps[i]))
      cat(sprintf("  Max Failures/s: %.2f, Total Failures: %d\n", 
                 endpoint_data$max_failures[i], endpoint_data$total_failures[i]))
      cat(sprintf("  Max P95 Latency: %.2f ms, Avg Latency: %.2f ms\n", 
                 endpoint_data$max_p95_latency[i], endpoint_data$avg_latency[i]))
    }
  } else {
    cat("\nNo endpoint-specific data available\n")
  }
  
  return(list(overall = if (exists("summary")) summary else NULL, 
              endpoints = endpoint_data))
}

# Main execution
tryCatch({
  # Process data
  cat("Processing data...\n")
  data <- process_data(input_file)
  
  # Generate and save plots
  cat("Generating plots...\n")
  
  # Plot: Faceted plot for each API endpoint
  p_endpoint <- create_endpoint_plot(data)
  if (!is.null(p_endpoint)) {
    # Adjust width based on number of endpoints (more endpoints need wider plot)
    endpoint_count <- length(unique(data$Endpoint[data$Name != "Aggregated"]))
    width <- min(16, max(10, ceiling(endpoint_count/4) * 8))
    height <- min(14, max(6, ceiling(endpoint_count/4) * 4))
    
    ggsave(paste0(output_file, ".png"), p_endpoint, width = width, height = height, dpi = 300)
    ggsave(paste0(output_file, ".pdf"), p_endpoint, width = width, height = height)
    cat(sprintf("- %s.png/pdf: Faceted endpoint performance visualization\n", output_file))
  } else {
    cat("No endpoint-specific data available for plotting\n")
  }
  
  # Generate summary statistics
  cat("\nGenerating summary statistics...\n")
  summary <- generate_summary(data)
  
  cat("\nPlots saved successfully!\n")
  
}, error = function(e) {
  cat(sprintf("Error: %s\n", e$message))
  quit(status = 1)
}) 