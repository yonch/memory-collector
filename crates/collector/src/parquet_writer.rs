use std::io::Write;
use std::sync::Arc;

use anyhow::{anyhow, Result};
use arrow_array::builder::{Int32Builder, Int64Builder, StringBuilder};
use arrow_array::{ArrayRef, RecordBatch};
use arrow_schema::{DataType, Field, Schema, SchemaRef};
use parquet::arrow::arrow_writer::ArrowWriter;

use crate::timeslot_data::TimeslotData;

/// Handles writing timeslot data to a Parquet file
pub struct ParquetWriter<W: Write + Send> {
    writer: ArrowWriter<W>,
    schema: SchemaRef,
}

impl<W: Write + Send> ParquetWriter<W> {
    /// Creates a new ParquetWriter with the provided writer
    pub fn new(writer: W) -> Result<Self> {
        // Define the schema for our Parquet file
        let schema = Arc::new(Schema::new(vec![
            Field::new("start_time", DataType::Int64, false),
            Field::new("pid", DataType::Int32, false),
            Field::new("process_name", DataType::Utf8, true),
            Field::new("cycles", DataType::Int64, false),
            Field::new("instructions", DataType::Int64, false),
            Field::new("llc_misses", DataType::Int64, false),
            Field::new("duration", DataType::Int64, false),
        ]));

        // Create an ArrowWriter with the schema
        let arrow_writer = ArrowWriter::try_new(writer, schema.clone(), None)
            .map_err(|e| anyhow!("Failed to create Arrow writer: {}", e))?;

        Ok(Self {
            writer: arrow_writer,
            schema,
        })
    }

    /// Writes a TimeslotData to the Parquet file
    pub fn write(&mut self, timeslot: &TimeslotData) -> Result<()> {
        // Convert the timeslot data to a RecordBatch
        let batch = self.timeslot_to_batch(timeslot)?;

        // Write the batch to the Parquet file
        self.writer
            .write(&batch)
            .map_err(|e| anyhow!("Failed to write batch to Parquet: {}", e))?;

        Ok(())
    }

    /// Closes the writer, finishing the Parquet file
    pub fn close(self) -> Result<()> {
        // ArrowWriter::close returns FileMetaData on success, but we just want to return ()
        self.writer
            .close()
            .map(|_| ())
            .map_err(|e| anyhow!("Failed to close Parquet writer: {}", e))
    }

    /// Converts a TimeslotData to an Arrow RecordBatch
    fn timeslot_to_batch(&self, timeslot: &TimeslotData) -> Result<RecordBatch> {
        // Get the task count to preallocate builders
        let task_count = timeslot.task_count();

        // Create array builders for each column
        let mut start_time_builder = Int64Builder::with_capacity(task_count);
        let mut pid_builder = Int32Builder::with_capacity(task_count);
        // For StringBuilder, we need both item capacity and estimated data capacity
        // Estimate 16 bytes per string for process names
        let mut process_name_builder = StringBuilder::with_capacity(task_count, task_count * 16);
        let mut cycles_builder = Int64Builder::with_capacity(task_count);
        let mut instructions_builder = Int64Builder::with_capacity(task_count);
        let mut llc_misses_builder = Int64Builder::with_capacity(task_count);
        let mut duration_builder = Int64Builder::with_capacity(task_count);

        // Convert timeslot data to arrays
        for (pid, task_data) in timeslot.iter_tasks() {
            // Add start timestamp (common for all tasks in this timeslot)
            start_time_builder.append_value(timeslot.start_timestamp as i64);

            // Add PID
            pid_builder.append_value(*pid as i32);

            // Add process name (from metadata if available)
            if let Some(ref metadata) = task_data.metadata {
                // Convert bytes to string, trimming null bytes
                let comm = std::str::from_utf8(&metadata.comm)
                    .unwrap_or("<invalid utf8>")
                    .trim_end_matches(char::from(0))
                    .to_string();
                process_name_builder.append_value(comm);
            } else {
                process_name_builder.append_null();
            }

            // Add metrics
            cycles_builder.append_value(task_data.metrics.cycles as i64);
            instructions_builder.append_value(task_data.metrics.instructions as i64);
            llc_misses_builder.append_value(task_data.metrics.llc_misses as i64);
            duration_builder.append_value(task_data.metrics.time_ns as i64);
        }

        // Finish building arrays
        let arrays: Vec<ArrayRef> = vec![
            Arc::new(start_time_builder.finish()),
            Arc::new(pid_builder.finish()),
            Arc::new(process_name_builder.finish()),
            Arc::new(cycles_builder.finish()),
            Arc::new(instructions_builder.finish()),
            Arc::new(llc_misses_builder.finish()),
            Arc::new(duration_builder.finish()),
        ];

        // Create and return the RecordBatch
        RecordBatch::try_new(self.schema.clone(), arrays)
            .map_err(|e| anyhow!("Failed to create RecordBatch: {}", e))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::metrics::Metric;
    use crate::task_metadata::TaskMetadata;
    use crate::timeslot_data::TimeslotData;
    use std::io::Cursor;

    #[test]
    fn test_parquet_writer() {
        // Create a test timeslot
        let mut timeslot = TimeslotData::new(1000000);

        // Create some test task data
        let mut comm = [0u8; 16];
        // Copy "test_process" into the comm array
        let test_name = b"test_process";
        comm[..test_name.len()].copy_from_slice(test_name);

        let metadata = Some(TaskMetadata::new(1, comm));
        let metrics = Metric::from_deltas(1000, 2000, 30, 100000);

        // Add task data to timeslot
        timeslot.update(1, metadata, metrics);

        // Create a test writer using an in-memory cursor
        let cursor = Cursor::new(Vec::new());
        let mut writer = ParquetWriter::new(cursor).unwrap();

        // Write the timeslot and close
        writer.write(&timeslot).unwrap();
        writer.close().unwrap();

        // Success if we got here without errors
        assert!(true);
    }
}
