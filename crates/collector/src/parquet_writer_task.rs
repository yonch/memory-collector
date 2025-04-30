use anyhow::Result;
use tokio::sync::{mpsc, watch};
use tokio::task::JoinHandle;

use crate::parquet_writer::ParquetWriter;
use crate::timeslot_data::TimeslotData;

/// Worker task for processing timeslots and writing them to parquet
pub struct ParquetWriterTask {
    sender: mpsc::Sender<TimeslotData>,
    shutdown_sender: watch::Sender<bool>,
    rotate_sender: mpsc::Sender<()>,
    join_handle: JoinHandle<Result<()>>,
}

impl ParquetWriterTask {
    /// Create a new ParquetWriterTask with a specified channel buffer size
    pub fn new(writer: ParquetWriter, buffer_size: usize) -> Self {
        // Create channels
        let (sender, receiver) = mpsc::channel::<TimeslotData>(buffer_size);
        let (shutdown_sender, shutdown_receiver) = watch::channel(false);
        let (rotate_sender, rotate_receiver) = mpsc::channel::<()>(1);

        // Create task runner
        let task_runner = TaskRunner {
            receiver,
            writer,
            shutdown_signal: shutdown_receiver,
            rotate_receiver,
        };

        // Spawn the task
        let join_handle = tokio::spawn(async move { task_runner.run().await });

        Self {
            sender,
            shutdown_sender,
            rotate_sender,
            join_handle,
        }
    }

    /// Get a sender that can be used to send TimeslotData to the task
    pub fn sender(&self) -> mpsc::Sender<TimeslotData> {
        self.sender.clone()
    }

    /// Shutdown the task and wait for it to complete
    pub async fn shutdown(self) -> Result<()> {
        // Signal the task to shut down
        self.signal_shutdown();

        // Wait for the task to complete
        match self.join_handle.await {
            Ok(result) => result,
            Err(e) => Err(anyhow::anyhow!("ParquetWriterTask panicked: {:?}", e)),
        }
    }

    /// Signal the task to shut down without waiting
    pub fn signal_shutdown(&self) {
        let _ = self.shutdown_sender.send(true);
    }

    /// Get the join handle to await task completion
    pub fn join_handle(&mut self) -> &mut JoinHandle<Result<()>> {
        &mut self.join_handle
    }

    /// Signal the task to rotate the current parquet file
    pub async fn rotate(&self) -> Result<()> {
        // Try to send rotation signal
        self.rotate_sender
            .send(())
            .await
            .map_err(|e| anyhow::anyhow!("Failed to send rotation signal: {}", e))?;
        Ok(())
    }
}

/// Internal task runner
struct TaskRunner {
    receiver: mpsc::Receiver<TimeslotData>,
    writer: ParquetWriter,
    shutdown_signal: watch::Receiver<bool>,
    rotate_receiver: mpsc::Receiver<()>,
}

impl TaskRunner {
    /// Run the task, processing timeslots until shutdown
    async fn run(mut self) -> Result<()> {
        while !*self.shutdown_signal.borrow() {
            tokio::select! {
                Some(timeslot) = self.receiver.recv() => {
                    // Convert timeslot to a batch
                    let batch = self.writer.timeslot_to_batch(timeslot)?;

                    // Write the batch
                    self.writer.write(batch).await?;
                }
                _ = self.shutdown_signal.changed() => {
                    // Shutdown signal received
                    break;
                }
                Some(_) = self.rotate_receiver.recv() => {
                    // Rotation signal received
                    if let Err(e) = self.writer.rotate().await {
                        log::warn!("Failed to rotate parquet file: {}", e);
                    } else {
                        log::info!("Parquet file rotated successfully");
                    }
                }
            }
        }

        // Close on shutdown
        self.writer.close().await
    }
}
