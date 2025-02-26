#!/usr/bin/env Rscript

# Check for required packages and install if missing
required_packages <- c("ggplot2", "dplyr", "tidyr", "optparse", "scales")
new_packages <- required_packages[!(required_packages %in% installed.packages()[,"Package"])]
if(length(new_packages)) install.packages(new_packages)

library(ggplot2)
library(dplyr)
library(tidyr)
library(optparse)
library(scales)

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
        panel.grid.minor=element_blank(),
        strip.text=element_text(size=12, face="bold"),
        plot.title=element_text(size=14, face="bold"),
        plot.subtitle=element_text(size=10)
    )

# Color palette
experiment_colors <- scale_color_brewer(palette="Set2")
experiment_fills <- scale_fill_brewer(palette="Set2")

# Function to create time series plot
create_time_series <- function(data, title_suffix="") {
    ggplot(data, aes(x=relative_time)) +
        # Min-max lines with minimum width for visibility
        geom_segment(aes(xend=relative_time, y=min_delay_us, yend=max_delay_us, color=experiment),
                    linewidth=0.2) +
        # Mean points
        geom_point(aes(y=mean_delay_us, color=experiment), size=0.5, alpha=0.7) +
        labs(x="Time (seconds)",
             y="Timer Delay (\u00B5s)",
             title=paste("Sync Timer Benchmark Results", title_suffix),
             subtitle="Points show mean delay, vertical lines show min-max range") +
        base_theme +
        experiment_colors +
        theme(
            legend.position="none",
            panel.spacing=unit(1, "lines"),
            strip.text.x=element_text(margin=margin(b=10)),
            plot.margin=margin(t=10, r=10, b=10, l=10)
        )
}

# 1. Vertical layout
p_vertical <- create_time_series(data) +
    facet_wrap(~experiment, ncol=1, scales="free_y")

ggsave(paste0(opt$prefix, "_vertical.pdf"), p_vertical, width=opt$width, height=opt$height)

# 2. Horizontal layout
p_horizontal <- create_time_series(data) +
    facet_wrap(~experiment, nrow=1, scales="free_y")

ggsave(paste0(opt$prefix, "_horizontal.pdf"), p_horizontal, width=opt$width, height=opt$height)

# 3. Hybrid layout (2x4 grid)
p_hybrid <- create_time_series(data) +
    facet_wrap(~experiment, nrow=2, scales="free_y")

ggsave(paste0(opt$prefix, "_hybrid.pdf"), p_hybrid, width=opt$width, height=opt$height)

# 4. Short duration plots (500ms)
data_short <- data %>%
    group_by(experiment) %>%
    filter(relative_time <= 0.5)  # 500ms

p_short_vertical <- create_time_series(data_short, "(500ms)") +
    facet_wrap(~experiment, ncol=1, scales="free_y")

ggsave(paste0(opt$prefix, "_short_vertical.pdf"), p_short_vertical, width=opt$width, height=opt$height)

p_short_horizontal <- create_time_series(data_short, "(500ms)") +
    facet_wrap(~experiment, nrow=1, scales="free_y")

ggsave(paste0(opt$prefix, "_short_horizontal.pdf"), p_short_horizontal, width=opt$width, height=opt$height)

p_short_hybrid <- create_time_series(data_short, "(500ms)") +
    facet_wrap(~experiment, nrow=2, scales="free_y")

ggsave(paste0(opt$prefix, "_short_hybrid.pdf"), p_short_hybrid, width=opt$width, height=opt$height)

# 5. CDF plots
# Prepare data for CDF plots
density_data <- data %>%
    select(experiment, mean_delay_us, max_delay_us, range_us) %>%
    gather(metric, value, -experiment) %>%
    group_by(experiment, metric) %>%
    arrange(value) %>%
    mutate(cdf = row_number() / n())

# Create labels for metrics
metric_labels <- c(
    mean_delay_us = "Mean Delay CDF",
    max_delay_us = "Maximum Delay CDF",
    range_us = "Delay Range CDF"
)

p_cdf <- ggplot(density_data, aes(x=value, y=cdf, color=experiment)) +
    geom_step(linewidth=1) +
    facet_wrap(~metric, nrow=1, scales="fixed", 
               labeller=labeller(metric=metric_labels)) +
    scale_x_log10(
        limits=c(1, 500),
        breaks=c(1, 2, 5, 10, 20, 50, 100, 200, 500),
        labels=function(x) paste0(x, " \u00B5s")
    ) +
    scale_y_continuous(labels=scales::percent) +
    labs(x="Delay",
         y="Cumulative Probability",
         title="Sync Timer Delay Distributions",
         subtitle="Cumulative distribution functions (log scale)") +
    base_theme +
    experiment_colors +
    theme(
        panel.spacing=unit(2, "lines"),
        strip.text.x=element_text(margin=margin(b=10)),
        plot.margin=margin(t=10, r=10, b=10, l=10)
    )

ggsave(paste0(opt$prefix, "_cdf.pdf"), p_cdf, width=opt$width, height=opt$height)

cat(sprintf("Plots saved with prefix: %s\n", opt$prefix)) 