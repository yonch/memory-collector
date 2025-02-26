#!/usr/bin/env Rscript

# Check for required packages and install if missing
required_packages <- c("ggplot2", "dplyr", "tidyr", "optparse")
new_packages <- required_packages[!(required_packages %in% installed.packages()[,"Package"])]
if(length(new_packages)) install.packages(new_packages)

library(ggplot2)
library(dplyr)
library(tidyr)
library(optparse)

# Parse command line arguments
option_list = list(
    make_option(c("-i", "--input"), type="character", default=NULL, 
                help="Input CSV file", metavar="character"),
    make_option(c("-o", "--output"), type="character", default="benchmark_plot.pdf",
                help="Output plot file [default= %default]", metavar="character"),
    make_option(c("-w", "--width"), type="numeric", default=12,
                help="Plot width in inches [default= %default]", metavar="number"),
    make_option(c("-h", "--height"), type="numeric", default=8,
                help="Plot height in inches [default= %default]", metavar="number")
)

opt_parser = OptionParser(option_list=option_list)
opt = parse_args(opt_parser)

if (is.null(opt$input)) {
    print_help(opt_parser)
    stop("Input CSV file must be specified.", call.=FALSE)
}

# Read data
data <- read.csv(opt$input)

# Convert timestamp to relative seconds from start
data <- data %>%
    group_by(experiment) %>%
    mutate(
        relative_time = (timestamp - min(timestamp)) / 1e9,  # Convert ns to s
        mean_delay_us = mean_delay / 1000,  # Convert ns to μs
        min_delay_us = min_delay / 1000,
        max_delay_us = max_delay / 1000,
        stddev_us = stddev / 1000
    )

# Create plot
p <- ggplot(data, aes(x=relative_time)) +
    # Plot min-max range
    geom_ribbon(aes(ymin=min_delay_us, ymax=max_delay_us, fill=experiment), alpha=0.2) +
    # Plot mean with points
    geom_point(aes(y=mean_delay_us, color=experiment), size=1, alpha=0.5) +
    # Add error bars for standard deviation
    geom_errorbar(aes(ymin=mean_delay_us-stddev_us, ymax=mean_delay_us+stddev_us, color=experiment),
                 width=0.1, alpha=0.3) +
    # Facet by experiment
    facet_wrap(~experiment, ncol=1, scales="free_y") +
    # Labels and theme
    labs(x="Time (seconds)",
         y="Timer Delay (μs)",
         title="Sync Timer Benchmark Results",
         subtitle="Points show mean delay, bands show min-max range, error bars show ±1σ") +
    theme_minimal() +
    theme(
        legend.position="none",
        panel.grid.minor=element_blank(),
        strip.text=element_text(size=12, face="bold"),
        plot.title=element_text(size=14, face="bold"),
        plot.subtitle=element_text(size=10)
    )

# Save plot
ggsave(opt$output, p, width=opt$width, height=opt$height)
cat(sprintf("Plot saved to %s\n", opt$output)) 