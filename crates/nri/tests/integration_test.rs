use std::sync::Arc;
use tokio::sync::Mutex;
use ttrpc::context::Context;
use ttrpc::r#async::transport::Socket;
use ttrpc::r#async::Client;
use ttrpc::r#async::Server;
use ttrpc::r#async::TtrpcContext;

use nri::api::{
    ConfigureRequest, ConfigureResponse, CreateContainerRequest, CreateContainerResponse, Empty,
    RegisterPluginRequest, StateChangeEvent, StopContainerRequest, StopContainerResponse,
    SynchronizeRequest, SynchronizeResponse, UpdateContainerRequest, UpdateContainerResponse,
    UpdateContainersRequest, UpdateContainersResponse, UpdatePodSandboxRequest,
    UpdatePodSandboxResponse,
};
use nri::api_ttrpc::{self, Plugin, PluginClient, Runtime, RuntimeClient};
use nri::multiplex::{Mux, PLUGIN_SERVICE_CONN, RUNTIME_SERVICE_CONN};
use protobuf::SpecialFields;

// Mock Runtime service implementation
#[derive(Clone)]
struct MockRuntimeService {
    runtime_mux: Arc<Mux>,
}

#[async_trait::async_trait]
impl Runtime for MockRuntimeService {
    async fn register_plugin(
        &self,
        _ctx: &TtrpcContext,
        req: RegisterPluginRequest,
    ) -> ttrpc::Result<Empty> {
        println!(
            "Mock Runtime: Received register_plugin request from plugin: {}",
            req.plugin_name
        );

        // Open plugin socket using the runtime mux
        let plugin_socket = self
            .runtime_mux
            .open(PLUGIN_SERVICE_CONN)
            .await
            .map_err(|e| {
                ttrpc::Error::RpcStatus(ttrpc::get_status(
                    ttrpc::Code::INTERNAL,
                    format!("Failed to open plugin socket: {}", e),
                ))
            })?;

        // Create plugin client
        let plugin_client = PluginClient::new(Client::new(Socket::new(plugin_socket)));

        // Send configure request to plugin
        let configure_req = ConfigureRequest {
            config: "test-config".to_string(),
            runtime_name: "test-runtime".to_string(),
            runtime_version: "1.0.0".to_string(),
            registration_timeout: 5000,
            request_timeout: 5000,
            special_fields: SpecialFields::default(),
        };

        let configure_resp = plugin_client
            .configure(Context::default(), &configure_req)
            .await?;
        assert_eq!(configure_resp.events, 0);

        // Send synchronize request to plugin
        let sync_req = SynchronizeRequest {
            pods: vec![],
            containers: vec![],
            more: false,
            special_fields: SpecialFields::default(),
        };

        let sync_resp = plugin_client
            .synchronize(Context::default(), &sync_req)
            .await?;
        assert_eq!(sync_resp.update.len(), 0);
        assert!(!sync_resp.more);

        Ok(Empty::default())
    }

    async fn update_containers(
        &self,
        _ctx: &TtrpcContext,
        _req: UpdateContainersRequest,
    ) -> ttrpc::Result<UpdateContainersResponse> {
        Ok(UpdateContainersResponse::default())
    }
}

// Mock Plugin service implementation
#[derive(Clone)]
struct MockPluginService {
    configured: Arc<Mutex<bool>>,
    synchronized: Arc<Mutex<bool>>,
}

#[async_trait::async_trait]
impl Plugin for MockPluginService {
    async fn configure(
        &self,
        _ctx: &TtrpcContext,
        req: ConfigureRequest,
    ) -> ttrpc::Result<ConfigureResponse> {
        println!(
            "Mock Plugin: Received configure request from runtime: {}",
            req.runtime_name
        );
        let mut configured = self.configured.lock().await;
        *configured = true;
        Ok(ConfigureResponse {
            events: 0, // Subscribe to no events for this test
            special_fields: SpecialFields::default(),
        })
    }

    async fn synchronize(
        &self,
        _ctx: &TtrpcContext,
        _req: SynchronizeRequest,
    ) -> ttrpc::Result<SynchronizeResponse> {
        println!("Mock Plugin: Received synchronize request from runtime");
        let mut synchronized = self.synchronized.lock().await;
        *synchronized = true;
        Ok(SynchronizeResponse {
            update: vec![],
            more: false,
            special_fields: SpecialFields::default(),
        })
    }

    async fn shutdown(&self, _ctx: &TtrpcContext, _req: Empty) -> ttrpc::Result<Empty> {
        Ok(Empty::default())
    }

    async fn create_container(
        &self,
        _ctx: &TtrpcContext,
        _req: CreateContainerRequest,
    ) -> ttrpc::Result<CreateContainerResponse> {
        Ok(CreateContainerResponse::default())
    }

    async fn update_container(
        &self,
        _ctx: &TtrpcContext,
        _req: UpdateContainerRequest,
    ) -> ttrpc::Result<UpdateContainerResponse> {
        Ok(UpdateContainerResponse::default())
    }

    async fn stop_container(
        &self,
        _ctx: &TtrpcContext,
        _req: StopContainerRequest,
    ) -> ttrpc::Result<StopContainerResponse> {
        Ok(StopContainerResponse::default())
    }

    async fn update_pod_sandbox(
        &self,
        _ctx: &TtrpcContext,
        _req: UpdatePodSandboxRequest,
    ) -> ttrpc::Result<UpdatePodSandboxResponse> {
        Ok(UpdatePodSandboxResponse::default())
    }

    async fn state_change(
        &self,
        _ctx: &TtrpcContext,
        _req: StateChangeEvent,
    ) -> ttrpc::Result<Empty> {
        Ok(Empty::default())
    }
}

#[tokio::test]
async fn test_nri_plugin_registration_workflow() -> Result<(), Box<dyn std::error::Error>> {
    // Create a duplex pipe for communication
    let (runtime_stream, plugin_stream) = tokio::io::duplex(1024);

    // Create multiplexers for both ends
    let runtime_mux = Arc::new(Mux::new(runtime_stream));
    let plugin_mux = Mux::new(plugin_stream);

    // Create mock services
    let runtime_service = Arc::new(MockRuntimeService {
        runtime_mux: runtime_mux.clone(),
    });

    // Create plugin service with state
    let plugin_configured = Arc::new(Mutex::new(false));
    let plugin_synchronized = Arc::new(Mutex::new(false));
    let plugin_service = Arc::new(MockPluginService {
        configured: plugin_configured.clone(),
        synchronized: plugin_synchronized.clone(),
    });

    // Create service maps
    let plugin_service_map = api_ttrpc::create_plugin(plugin_service);
    let runtime_service_map = api_ttrpc::create_runtime(runtime_service.clone());

    // Create servers
    let mut plugin_server = Server::new().register_service(plugin_service_map);
    let mut runtime_server = Server::new().register_service(runtime_service_map);

    // Start servers
    let plugin_socket = plugin_mux.open(PLUGIN_SERVICE_CONN).await.unwrap();
    let plugin_server_handle = tokio::spawn(async move {
        plugin_server
            .start_connected(Socket::new(plugin_socket))
            .await
    });

    let runtime_server_handle = tokio::spawn(async move {
        let runtime_socket = runtime_mux.open(RUNTIME_SERVICE_CONN).await.unwrap();
        runtime_server
            .start_connected(Socket::new(runtime_socket))
            .await
    });

    // Create runtime client using a new clone of runtime_mux
    let runtime_client = RuntimeClient::new(Client::new(Socket::new(
        plugin_mux.open(RUNTIME_SERVICE_CONN).await?,
    )));

    // Register plugin with runtime
    let register_req = RegisterPluginRequest {
        plugin_name: "test-plugin".to_string(),
        plugin_idx: "0".to_string(),
        special_fields: SpecialFields::default(),
    };

    runtime_client
        .register_plugin(Context::default(), &register_req)
        .await?;

    // Verify plugin state using the cloned state
    let configured = *plugin_configured.lock().await;
    let synchronized = *plugin_synchronized.lock().await;
    assert!(configured, "Plugin should be configured");
    assert!(synchronized, "Plugin should be synchronized");

    // Clean up
    plugin_server_handle.abort();
    runtime_server_handle.abort();

    Ok(())
}
