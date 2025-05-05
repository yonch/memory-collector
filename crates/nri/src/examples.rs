//! Examples showing how to use the NRI client and server

use anyhow::Result;
use async_trait::async_trait;
use std::sync::Arc;

use crate::api::{
    ConfigureRequest, ConfigureResponse, Empty, UpdateContainerRequest, UpdateContainerResponse,
    UpdatePodSandboxRequest, UpdatePodSandboxResponse,
};
use crate::api_ttrpc::{Plugin, Runtime};
use ttrpc::r#async::TtrpcContext; // Using the async context for our examples

/// Example of an NRI plugin implementation
pub struct ExamplePlugin;

#[async_trait]
impl Plugin for ExamplePlugin {
    async fn configure(
        &self,
        _ctx: &TtrpcContext,
        req: ConfigureRequest,
    ) -> ttrpc::Result<ConfigureResponse> {
        println!("Received Configure request from runtime: {:?}", req.runtime);

        // Create a response
        let mut response = ConfigureResponse::new();

        Ok(response)
    }

    async fn update_container(
        &self,
        _ctx: &TtrpcContext,
        req: UpdateContainerRequest,
    ) -> ttrpc::Result<UpdateContainerResponse> {
        println!("Container update request received");

        // Print some information about the container if available
        if let Some(container) = &req.container {
            println!("  Container ID: {}", container.id);
            println!("  Container name: {}", container.name);
            println!("  Pod ID: {}", container.pod_id);
        }

        Ok(UpdateContainerResponse::new())
    }

    async fn update_pod_sandbox(
        &self,
        _ctx: &TtrpcContext,
        req: UpdatePodSandboxRequest,
    ) -> ttrpc::Result<UpdatePodSandboxResponse> {
        println!("Pod update request received");

        // Print some information about the pod if available
        if let Some(pod) = &req.pod_sandbox {
            println!("  Pod ID: {}", pod.id);
            println!("  Pod name: {}", pod.name);
            println!("  Pod namespace: {}", pod.namespace);
        }

        Ok(UpdatePodSandboxResponse::new())
    }
}

/// Example of an NRI runtime implementation
pub struct ExampleRuntime;

#[async_trait]
impl Runtime for ExampleRuntime {
    // Runtime manages the container runtimes and NRI plugins don't typically implement this
}

/// Example showing how to create and start a plugin server
pub async fn example_plugin_server() -> Result<()> {
    use crate::server::create_async_plugin_server;

    let socket_path = "/tmp/nri-plugin.sock";
    let service = ExamplePlugin {};

    let server = create_async_plugin_server(socket_path, service)?;

    // Start the server
    server.start()?;

    println!("NRI plugin server started on {}", socket_path);

    // In a real application, you would keep the server running
    std::thread::sleep(std::time::Duration::from_secs(3600));

    Ok(())
}

/// Example showing how to create and connect a runtime client to talk to plugins
pub async fn example_runtime_client() -> Result<()> {
    use crate::client::create_runtime_client;

    let socket_path = "/tmp/nri-plugin.sock";
    let client = create_runtime_client(socket_path)?;

    let ctx = ttrpc::context::Context::default();

    // Create a RegisterPlugin request
    let mut request = crate::api::RegisterPluginRequest::new();
    request.plugin_name = "example-plugin".to_string();
    request.plugin_idx = "0".to_string();

    // Call the service
    let response = client.register_plugin(&ctx, &request).await?;

    println!("Got response from plugin registration");

    Ok(())
}
