#!/usr/bin/env Rscript

# Check for required packages and install if missing
required_packages <- c("ggplot2", "dplyr", "tidyr", "optparse", "ggridges")
new_packages <- required_packages[!(required_packages %in% installed.packages()[,"Package"])]
if(length(new_packages)) install.packages(new_packages)

library(ggplot2)
library(dplyr)
library(tidyr)
library(optparse)
library(ggridges)

# Parse command line arguments
option_list = list(
    make_option(c("-i", "--input"), type="character", default=NULL, 
                help="Input CSV file", metavar="character"),
    make_option(c("-p", "--prefix"), type="character", default="benchmark",
                help="Output file prefix [default= %default]", metavar="character"),
    make_option(c("-w", "--width"), type="numeric", default=16,
                help="Plot width in inches [default= %default]", metavar="number"),
    make_option(c("-h", "--height"), type="numeric", default=9,
                help="Plot height in inches [default= %default]", metavar="number")
)

opt_parser = OptionParser(option_list=option_list, add_help_option=FALSE)
opt = parse_args(opt_parser)

if (is.null(opt$input)) {
    print_help(opt_parser)
    stop("Input CSV file must be specified.", call.=FALSE)
}

# Read data
data <- read.csv(opt$input)

# Convert timestamp to relative seconds from start and other unit conversions
data <- data %>%
    group_by(experiment) %>%
    mutate(
        relative_time = (timestamp - min(timestamp)) / 1e9,  # Convert ns to s
        mean_delay_us = mean_delay / 1000,  # Convert ns to μs
        min_delay_us = min_delay / 1000,
        max_delay_us = max_delay / 1000,
        range_us = max_delay_us - min_delay_us
    )

# Common theme settings
base_theme <- theme_minimal() +
    theme(
        legend.position="right",
        panel.grid.minor=element_blank(),
        strip.text=element_text(size=12, face="bold"),
        plot.title=element_text(size=14, face="bold"),
        plot.subtitle=element_text(size=10),
        aspect.ratio=9/16
    )

# Color palette
experiment_colors <- scale_color_brewer(palette="Set2")
experiment_fills <- scale_fill_brewer(palette="Set2")

# 1. Main time series plot (vertical)
p_vertical <- ggplot(data, aes(x=relative_time)) +
    geom_ribbon(aes(ymin=min_delay_us, ymax=max_delay_us, fill=experiment), alpha=0.3) +
    geom_point(aes(y=mean_delay_us, color=experiment), size=0.5, alpha=0.7) +
    facet_wrap(~experiment, ncol=1, scales="free_y") +
    labs(x="Time (seconds)",
         y="Timer Delay (μs)",
         title="Sync Timer Benchmark Results",
         subtitle="Points show mean delay, bands show min-max range") +
    base_theme +
    experiment_colors +
    experiment_fills

ggsave(paste0(opt$prefix, "_vertical.pdf"), p_vertical, width=opt$width, height=opt$height)

# 2. Time series plot (horizontal)
p_horizontal <- ggplot(data, aes(x=relative_time)) +
    geom_ribbon(aes(ymin=min_delay_us, ymax=max_delay_us, fill=experiment), alpha=0.3) +
    geom_point(aes(y=mean_delay_us, color=experiment), size=0.5, alpha=0.7) +
    facet_wrap(~experiment, nrow=1, scales="free_y") +
    labs(x="Time (seconds)",
         y="Timer Delay (μs)",
         title="Sync Timer Benchmark Results",
         subtitle="Points show mean delay, bands show min-max range") +
    base_theme +
    experiment_colors +
    experiment_fills

ggsave(paste0(opt$prefix, "_horizontal.pdf"), p_horizontal, width=opt$width, height=opt$height)

# 3. Short duration time series (500ms, vertical)
data_short <- data %>%
    group_by(experiment) %>%
    filter(relative_time <= 0.5)  # 500ms

p_short_vertical <- ggplot(data_short, aes(x=relative_time)) +
    geom_ribbon(aes(ymin=min_delay_us, ymax=max_delay_us, fill=experiment), alpha=0.3) +
    geom_point(aes(y=mean_delay_us, color=experiment), size=0.5, alpha=0.7) +
    facet_wrap(~experiment, ncol=1, scales="free_y") +
    labs(x="Time (seconds)",
         y="Timer Delay (μs)",
         title="Sync Timer Benchmark Results (500ms)",
         subtitle="Points show mean delay, bands show min-max range") +
    base_theme +
    experiment_colors +
    experiment_fills

ggsave(paste0(opt$prefix, "_short_vertical.pdf"), p_short_vertical, width=opt$width, height=opt$height)

# 4. Short duration time series (500ms, horizontal)
p_short_horizontal <- ggplot(data_short, aes(x=relative_time)) +
    geom_ribbon(aes(ymin=min_delay_us, ymax=max_delay_us, fill=experiment), alpha=0.3) +
    geom_point(aes(y=mean_delay_us, color=experiment), size=0.5, alpha=0.7) +
    facet_wrap(~experiment, nrow=1, scales="free_y") +
    labs(x="Time (seconds)",
         y="Timer Delay (μs)",
         title="Sync Timer Benchmark Results (500ms)",
         subtitle="Points show mean delay, bands show min-max range") +
    base_theme +
    experiment_colors +
    experiment_fills

ggsave(paste0(opt$prefix, "_short_horizontal.pdf"), p_short_horizontal, width=opt$width, height=opt$height)

# 5. Probability density plots
# Prepare data for density plots
density_data <- data %>%
    select(experiment, mean_delay_us, max_delay_us, range_us) %>%
    gather(metric, value, -experiment)

# Create labels for metrics
metric_labels <- c(
    mean_delay_us = "Mean Delay Distribution",
    max_delay_us = "Maximum Delay Distribution",
    range_us = "Delay Range Distribution"
)

p_density <- ggplot(density_data, aes(x=value, y=experiment, fill=experiment)) +
    geom_density_ridges(alpha=0.6, scale=2) +
    facet_wrap(~metric, scales="free_x", ncol=1, 
               labeller=labeller(metric=metric_labels)) +
    labs(x="Delay (μs)",
         y="Experiment",
         title="Sync Timer Delay Distributions",
         subtitle="Density plots showing delay characteristics across experiments") +
    base_theme +
    experiment_fills +
    theme(legend.position="none")

ggsave(paste0(opt$prefix, "_density.pdf"), p_density, width=opt$width, height=opt$height)

cat(sprintf("Plots saved with prefix: %s\n", opt$prefix)) 