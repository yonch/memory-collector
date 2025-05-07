// Re-export generated protobuf code
#![allow(clippy::all)]
#![allow(unused_imports)]

// Include the generated code
include!(concat!(env!("OUT_DIR"), "/mod.rs"));

// Re-export the generated ttrpc code
pub mod api_ttrpc {
    // Add crate-level attributes to avoid issues with inner attributes in the generated code
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    #![allow(non_upper_case_globals)]
    #![allow(dead_code)]
    #![allow(clippy::all)]
    #![allow(unused_imports)]
    #![allow(unused_results)]
    #![allow(missing_docs)]
    #![allow(trivial_casts)]
    #![allow(unsafe_code)]
    #![allow(unknown_lints)]

    include!(concat!(env!("OUT_DIR"), "/api_ttrpc.rs"));
}

// Export the multiplexer module
pub mod multiplex;

// Export the multiplexer
pub use multiplex::Mux;

// Export types for convenience
pub mod types {
    // NRI doesn't have all the types we were originally expecting
    // Export what's actually available from the generated code
    pub use crate::api::ContainerState;
    pub use crate::api::LinuxNamespace;
    pub use crate::api::Mount;
}

// Export client for convenience
pub mod client {
    use anyhow::{anyhow, Result};
    use std::path::Path;

    use crate::api_ttrpc::{HostFunctionsClient, PluginClient, RuntimeClient};
    use crate::multiplex::{Mux, MuxSocket, RUNTIME_SERVICE_CONN};
    use ttrpc::r#async::Client;

    /// Create a Plugin client
    pub async fn create_plugin_client<P: AsRef<Path>>(socket_path: P) -> Result<PluginClient> {
        let client = Client::connect(socket_path.as_ref().to_str().unwrap()).await?;
        Ok(PluginClient::new(client))
    }

    /// Create a Runtime client
    pub async fn create_runtime_client<P: AsRef<Path>>(socket_path: P) -> Result<RuntimeClient> {
        let client = Client::connect(socket_path.as_ref().to_str().unwrap()).await?;
        Ok(RuntimeClient::new(client))
    }

    /// Create a HostFunctions client
    pub async fn create_host_functions_client<P: AsRef<Path>>(
        socket_path: P,
    ) -> Result<HostFunctionsClient> {
        let client = Client::connect(socket_path.as_ref().to_str().unwrap()).await?;
        Ok(HostFunctionsClient::new(client))
    }

    /// Create a Runtime client using the multiplexer
    pub async fn create_runtime_client_mux(mux: &Mux) -> Result<RuntimeClient> {
        let socket = mux
            .open(RUNTIME_SERVICE_CONN)
            .await
            .map_err(|e| anyhow!("Failed to open runtime connection: {}", e))?;
        // Convert the MuxSocket to a ttrpc Socket
        let ttrpc_socket = ttrpc::r#async::transport::Socket::new(socket);
        let client = Client::new(ttrpc_socket);
        Ok(RuntimeClient::new(client))
    }
}

// Export server for convenience
pub mod server {
    use anyhow::{anyhow, Result};
    use std::collections::HashMap;
    use std::path::Path;
    use std::sync::Arc;

    use crate::api_ttrpc::{
        create_host_functions, create_plugin, create_runtime, HostFunctions, Plugin, Runtime,
    };
    use crate::multiplex::{Mux, PLUGIN_SERVICE_CONN};
    use ttrpc::Server;

    // Helper to create an async server
    pub fn create_async_server<P: AsRef<Path>>(socket_path: P) -> Result<ttrpc::r#async::Server> {
        Ok(ttrpc::r#async::Server::new().bind(socket_path.as_ref().to_str().unwrap())?)
    }

    /// Create a Plugin server
    pub fn create_plugin_server<P: AsRef<Path>, S: Plugin + Send + 'static>(
        socket_path: P,
        service: S,
    ) -> Result<Server> {
        let _service = Arc::new(Box::new(service) as Box<dyn Plugin + Send + Sync>);

        // Create a sync server
        let server = Server::new().bind(socket_path.as_ref().to_str().unwrap())?;

        // Since we can't easily convert async to sync services with the current ttrpc version,
        // we recommend using the async version directly

        // Here's a placeholder implementation
        Ok(server)
    }

    /// Create a Runtime server
    pub fn create_runtime_server<P: AsRef<Path>, S: Runtime + Send + 'static>(
        socket_path: P,
        service: S,
    ) -> Result<Server> {
        let _service = Arc::new(Box::new(service) as Box<dyn Runtime + Send + Sync>);

        // Create a sync server
        let server = Server::new().bind(socket_path.as_ref().to_str().unwrap())?;

        // Since we can't easily convert async to sync services with the current ttrpc version,
        // we recommend using the async version directly

        // Here's a placeholder implementation
        Ok(server)
    }

    /// Create a HostFunctions server
    pub fn create_host_functions_server<P: AsRef<Path>, S: HostFunctions + Send + 'static>(
        socket_path: P,
        service: S,
    ) -> Result<Server> {
        let _service = Arc::new(Box::new(service) as Box<dyn HostFunctions + Send + Sync>);

        // Create a sync server
        let server = Server::new().bind(socket_path.as_ref().to_str().unwrap())?;

        // Since we can't easily convert async to sync services with the current ttrpc version,
        // we recommend using the async version directly

        // Here's a placeholder implementation
        Ok(server)
    }

    /// Create an async Plugin server (recommended)
    pub fn create_async_plugin_server<P: AsRef<Path>, S: Plugin + Send + 'static>(
        socket_path: P,
        service: S,
    ) -> Result<ttrpc::r#async::Server> {
        let service = Arc::new(service);
        let service_mapper = create_plugin(service);

        let server = ttrpc::r#async::Server::new()
            .bind(socket_path.as_ref().to_str().unwrap())?
            .register_service(service_mapper);

        Ok(server)
    }

    /// Create an async Plugin server using the multiplexer
    pub async fn create_async_plugin_server_mux<S: Plugin + Send + 'static>(
        mux: &Mux,
        service: S,
    ) -> Result<(ttrpc::r#async::Server, ttrpc::r#async::transport::Socket)> {
        let mux_socket = mux
            .open(PLUGIN_SERVICE_CONN)
            .await
            .map_err(|e| anyhow!("Failed to open plugin connection: {}", e))?;

        // Convert the MuxSocket to a ttrpc Socket and store for use with the server
        let ttrpc_socket = ttrpc::r#async::transport::Socket::new(mux_socket);

        let service = Arc::new(service);
        let service_mapper = create_plugin(service);

        let server = ttrpc::r#async::Server::new().register_service(service_mapper);

        // Return both the server and the socket, so the caller can use server.start(ttrpc_socket)
        Ok((server, ttrpc_socket))
    }

    /// Create an async Runtime server (recommended)
    pub fn create_async_runtime_server<P: AsRef<Path>, S: Runtime + Send + 'static>(
        socket_path: P,
        service: S,
    ) -> Result<ttrpc::r#async::Server> {
        let service = Arc::new(service);
        let service_mapper = create_runtime(service);

        let server = ttrpc::r#async::Server::new()
            .bind(socket_path.as_ref().to_str().unwrap())?
            .register_service(service_mapper);

        Ok(server)
    }

    /// Create an async HostFunctions server (recommended)
    pub fn create_async_host_functions_server<P: AsRef<Path>, S: HostFunctions + Send + 'static>(
        socket_path: P,
        service: S,
    ) -> Result<ttrpc::r#async::Server> {
        let service = Arc::new(service);
        let service_mapper = create_host_functions(service);

        let server = ttrpc::r#async::Server::new()
            .bind(socket_path.as_ref().to_str().unwrap())?
            .register_service(service_mapper);

        Ok(server)
    }
}

// Include examples
#[cfg(feature = "examples")]
pub mod examples;
