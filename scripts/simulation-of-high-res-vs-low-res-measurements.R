# Memory Bandwidth Utilization Simulation
# Demonstrates why measuring application interference requires fine time granularity

# Load necessary libraries
library(ggplot2)
library(dplyr)
library(tidyr)
library(patchwork) # For combining plots

# Parse command line arguments for time window (default: 10 seconds)
args <- commandArgs(trailingOnly = TRUE)
time_window <- if(length(args) > 0) as.numeric(args[1]) else 10

# Simulation parameters
num_apps <- 8
time_step_ms <- 10 # 10ms granularity for simulation
total_steps <- time_window * 1000 / time_step_ms # Number of time steps
base_utilization_total <- 20 # Total base utilization (%) when no events
base_utilization_per_app <- base_utilization_total / num_apps # Base per app

# Parameters for the load comparison simulation
comparison_time_window <- 6 # 6 seconds for the side-by-side comparison
low_load_factor <- 0.25 # Factor to reduce both event frequency and base utilization

# Function to generate random high-bandwidth events with adjustable frequency
generate_events <- function(total_steps, time_window, time_step_ms, event_probability = 1.0) {
  events <- numeric(total_steps)
  
  # Generate events with specified probability per second for each app
  for(second in 0:(time_window-1)) {
    # Only generate an event with the given probability
    if(runif(1) <= event_probability) {
      # Random offset within the second
      event_start_ms <- second * 1000 + runif(1, 0, 800) # Leave room for longer events
      event_start_step <- floor(event_start_ms / time_step_ms) + 1
      
      # Determine event duration (10-100ms)
      event_duration_ms <- runif(1, 10, 100)
      event_duration_steps <- ceiling(event_duration_ms / time_step_ms)
      
      # Make sure we don't go out of bounds
      if(event_start_step > total_steps) {
        next
      }
      
      # Mark the event in the vector
      end_step <- min(event_start_step + event_duration_steps - 1, total_steps)
      events[event_start_step:end_step] <- 1
    }
  }
  
  return(events)
}

# Function to generate data for a specific load level
generate_load_data <- function(time_window, event_probability, base_util_total) {
  # Calculate simulation parameters
  total_steps <- time_window * 1000 / time_step_ms
  base_util_per_app <- base_util_total / num_apps
  
  # Generate application data
  set.seed(42) # Use same seed for reproducibility
  app_data_load <- list()
  
  for(app_id in 1:num_apps) {
    # Generate high-bandwidth events for this app with specified probability
    events <- generate_events(total_steps, time_window, time_step_ms, event_probability)
    
    # Create time series for this app
    app_data_load[[app_id]] <- tibble(
      time_ms = seq(0, (total_steps - 1) * time_step_ms, by = time_step_ms),
      app_id = factor(paste0("App", app_id), levels = paste0("App", 1:num_apps)),
      has_event = events
    )
  }
  
  # Combine all app data
  all_app_data_load <- bind_rows(app_data_load)
  
  # Calculate how many apps have events at each time step
  event_counts_load <- all_app_data_load %>%
    group_by(time_ms) %>%
    summarize(event_count = sum(has_event))
  
  # Calculate bandwidth for each app at each time step
  bandwidth_data_load <- all_app_data_load %>%
    left_join(event_counts_load, by = "time_ms") %>%
    mutate(
      # If this app has an event and others do too, split bandwidth with some noise
      # Otherwise use base utilization with small random noise
      bandwidth = case_when(
        has_event == 1 & event_count > 0 ~ (100 / event_count) * runif(n(), 0.85, 1.15),
        TRUE ~ base_util_per_app * runif(n(), 0.7, 1.3)
      )
    )
  
  # Ensure bandwidth doesn't exceed 100% when stacked
  bandwidth_data_load <- bandwidth_data_load %>%
    group_by(time_ms) %>%
    mutate(
      total_bandwidth = sum(bandwidth),
      bandwidth = if_else(total_bandwidth > 100, 
                           bandwidth * (100 / total_bandwidth), 
                           bandwidth)
    ) %>%
    ungroup()
  
  return(bandwidth_data_load)
}

# Function to create 1-second aggregated data
create_low_res_data <- function(bandwidth_data) {
  low_res_data <- bandwidth_data %>%
    mutate(time_s = floor(time_ms / 1000)) %>%
    group_by(time_s, app_id) %>%
    summarize(bandwidth = mean(bandwidth), .groups = "drop") %>%
    mutate(time_ms = time_s * 1000)
  
  return(low_res_data)
}

# Function to create and save plots
create_plots <- function(bandwidth_data, low_res_data, time_window, 
                         high_res_title, low_res_title, 
                         high_res_filename, low_res_filename, combined_filename,
                         width = 12, height_high = 2.5, height_low = 3, height_combined = 5.5) {
  
  # Create a common theme for both plots
  common_theme <- theme_minimal() +
    theme(
      panel.grid.minor = element_blank(),
      axis.title = element_text(size = 10),
      plot.title = element_text(size = 11, face = "bold"),
      legend.text = element_text(size = 8),
      legend.key.size = unit(0.5, "cm")
    )
  
  # Create high-resolution plot (10ms)
  high_res_plot <- ggplot(bandwidth_data, aes(x = time_ms, y = bandwidth, fill = app_id)) +
    geom_area(alpha = 0.8, position = "stack") +
    scale_x_continuous(name = "Time (s)", 
                      breaks = seq(0, time_window * 1000, by = 1000),
                      labels = function(x) x / 1000) +
    scale_y_continuous(name = "Memory Bandwidth (%)", limits = c(0, 100)) +
    scale_fill_brewer(palette = "Set1") +
    common_theme +
    theme(legend.position = "none") +
    ggtitle(high_res_title)
  
  # Create low-resolution plot (1s)
  low_res_plot <- ggplot(low_res_data, aes(x = time_ms, y = bandwidth, fill = app_id)) +
    geom_area(alpha = 0.8, position = "stack") +
    scale_x_continuous(name = "Time (s)", 
                      breaks = seq(0, time_window * 1000, by = 1000),
                      labels = function(x) x / 1000) +
    scale_y_continuous(name = "Memory Bandwidth (%)", limits = c(0, 100)) +
    scale_fill_brewer(palette = "Set1") +
    common_theme +
    theme(legend.position = "bottom", legend.title = element_blank()) +
    guides(fill = guide_legend(nrow = 1)) +
    ggtitle(low_res_title)
  
  # Save individual plots
  ggsave(high_res_filename, high_res_plot, width = width, height = height_high, units = "in", dpi = 200)
  ggsave(low_res_filename, low_res_plot, width = width, height = height_low, units = "in", dpi = 200)
  
  # Save a combined version
  combined_plot <- high_res_plot / low_res_plot +
    plot_layout(ncol = 1, heights = c(1, 1.3)) & 
    theme(plot.margin = margin(0.2, 0.2, 0.2, 0.2, "cm"))
  
  ggsave(combined_filename, combined_plot, width = width, height = height_combined, units = "in", dpi = 200)
  
  return(list(high_res = high_res_plot, low_res = low_res_plot, combined = combined_plot))
}

# Function to create side-by-side comparison plots
create_comparison_plots <- function(high_load_data, low_load_data, time_window) {
  # Create a common theme
  common_theme <- theme_minimal() +
    theme(
      panel.grid.minor = element_blank(),
      axis.title = element_text(size = 10),
      plot.title = element_text(size = 11, face = "bold"),
      legend.text = element_text(size = 8),
      legend.key.size = unit(0.5, "cm")
    )
  
  # Create high-resolution plot for high load
  high_load_plot <- ggplot(high_load_data, aes(x = time_ms, y = bandwidth, fill = app_id)) +
    geom_area(alpha = 0.8, position = "stack") +
    scale_x_continuous(name = "Time (s)", 
                      breaks = seq(0, time_window * 1000, by = 1000),
                      labels = function(x) x / 1000) +
    scale_y_continuous(name = "Memory Bandwidth (%)", limits = c(0, 100)) +
    scale_fill_brewer(palette = "Set1") +
    common_theme +
    theme(legend.position = "none") +
    ggtitle("High System Load (10ms Granularity)")
  
  # Create high-resolution plot for low load
  low_load_plot <- ggplot(low_load_data, aes(x = time_ms, y = bandwidth, fill = app_id)) +
    geom_area(alpha = 0.8, position = "stack") +
    scale_x_continuous(name = "Time (s)", 
                      breaks = seq(0, time_window * 1000, by = 1000),
                      labels = function(x) x / 1000) +
    scale_y_continuous(name = "Memory Bandwidth (%)", limits = c(0, 100)) +
    scale_fill_brewer(palette = "Set1") +
    common_theme +
    theme(legend.position = "none") +  # Remove legend from low load plot
    ggtitle("Low System Load (10ms Granularity)")
  
  # Save individual load comparison plots
  ggsave("high_load_bandwidth.png", high_load_plot, width = 6, height = 2.5, units = "in", dpi = 200)
  ggsave("low_load_bandwidth.png", low_load_plot + 
           theme(legend.position = "bottom", legend.title = element_blank()) +
           guides(fill = guide_legend(nrow = 1)), 
         width = 6, height = 3, units = "in", dpi = 200)
  
  # Save a combined load comparison version (vertical)
  load_comparison_plot <- high_load_plot / (low_load_plot + 
                                             theme(legend.position = "bottom", legend.title = element_blank()) +
                                             guides(fill = guide_legend(nrow = 1))) +
    plot_layout(ncol = 1, heights = c(1, 1.3)) & 
    theme(plot.margin = margin(0.2, 0.2, 0.2, 0.2, "cm"))
  
  ggsave("load_comparison.png", load_comparison_plot, width = 6, height = 5.5, units = "in", dpi = 200)
  
  # Create side-by-side comparison with a shared legend at the bottom
  # First create a combined plot with no legend
  side_by_side_no_legend <- (high_load_plot + low_load_plot) +
    plot_layout(ncol = 2, widths = c(1, 1))
  
  # Then add a shared legend at the bottom
  side_by_side_with_legend <- side_by_side_no_legend + 
    plot_layout(guides = "collect") &
    theme(
      legend.position = "bottom",
      legend.title = element_blank(),
      plot.margin = margin(0.2, 0.2, 0.2, 0.2, "cm")
    ) &
    guides(fill = guide_legend(nrow = 1))
  
  ggsave("side_by_side_load_comparison.png", side_by_side_with_legend, width = 8, height = 3.5, units = "in", dpi = 200)
  
  return(list(
    high_load = high_load_plot, 
    low_load = low_load_plot, 
    vertical = load_comparison_plot, 
    side_by_side = side_by_side_with_legend
  ))
}

# Generate data for the original simulation
bandwidth_data <- generate_load_data(time_window, 1.0, base_utilization_total)
low_res_data <- create_low_res_data(bandwidth_data)

# Create and save the original plots
original_plots <- create_plots(
  bandwidth_data, 
  low_res_data, 
  time_window,
  "Memory Bandwidth Utilization (10ms Granularity)",
  "Memory Bandwidth Utilization (1s Granularity)",
  "high_res_bandwidth.png", 
  "low_res_bandwidth.png", 
  "memory_bandwidth_comparison.png"
)

cat("Original plots saved as:\n")
cat("- high_res_bandwidth.png (10ms granularity)\n")
cat("- low_res_bandwidth.png (1s granularity)\n")
cat("- memory_bandwidth_comparison.png (combined)\n")

# Generate data for high load and low load scenarios
high_load_data <- generate_load_data(comparison_time_window, 1.0, base_utilization_total)
low_load_data <- generate_load_data(comparison_time_window, low_load_factor, base_utilization_total * low_load_factor)

# Add a load type column to each dataset
high_load_data$load_type <- "High Load"
low_load_data$load_type <- "Low Load"

# Create and save the comparison plots
comparison_plots <- create_comparison_plots(high_load_data, low_load_data, comparison_time_window)

cat("\nLoad comparison plots saved as:\n")
cat("- high_load_bandwidth.png (high load, 10ms granularity)\n")
cat("- low_load_bandwidth.png (low load, 10ms granularity)\n")
cat("- load_comparison.png (vertical comparison)\n")
cat("- side_by_side_load_comparison.png (horizontal comparison for slides)\n")