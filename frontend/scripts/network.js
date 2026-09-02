/**
 * GameNetwork - Módulo de comunicación WebSocket entre el Backend C++ y Frontend JS
 * Encargado de recibir las actualizaciones de posición calculadas por los hilos del backend.
 */
class GameNetwork {
  constructor() {
    this.socket = null;
    this.isConnected = false;
    this.designName = "Diseño 1: Hilos Independientes";
    this.activeThreads = 0;
    this.backendCars = [];
    this.listeners = [];
    this.reconnectTimer = null;
    this.statusElement = null;

    this.createStatusOverlay();
    this.connect();
  }

  createStatusOverlay() {
    // Crear un panel de estado visual para monitorear el backend y la concurrencia
    const overlay = document.createElement('div');
    overlay.id = 'network-status-panel';
    overlay.style.position = 'fixed';
    overlay.style.top = '12px';
    overlay.style.left = '12px';
    overlay.style.backgroundColor = 'rgba(15, 23, 42, 0.88)';
    overlay.style.color = '#f8fafc';
    overlay.style.fontFamily = 'monospace, sans-serif';
    overlay.style.fontSize = '12px';
    overlay.style.padding = '10px 14px';
    overlay.style.borderRadius = '8px';
    overlay.style.boxShadow = '0 4px 12px rgba(0, 0, 0, 0.5)';
    overlay.style.border = '1px solid rgba(255, 255, 255, 0.15)';
    overlay.style.zIndex = '9999';
    overlay.style.lineHeight = '1.5';
    overlay.style.pointerEvents = 'none';

    overlay.innerHTML = `
      <div style="font-weight: bold; font-size: 13px; margin-bottom: 4px; color: #38bdf8;">
        🏎️ SISTEMA MULTIHILO (C++ BACKEND)
      </div>
      <div><strong>Diseño:</strong> <span id="net-design" style="color: #fbbf24;">Conectando...</span></div>
      <div><strong>Estado Socket:</strong> <span id="net-status" style="color: #ef4444;">🔴 Desconectado</span></div>
      <div><strong>Hilos Activos (Carros):</strong> <span id="net-threads" style="color: #4ade80;">0</span></div>
    `;

    document.body.appendChild(overlay);
    this.statusElement = overlay;
  }

  updateOverlay() {
    const designEl = document.getElementById('net-design');
    const statusEl = document.getElementById('net-status');
    const threadsEl = document.getElementById('net-threads');

    if (designEl) designEl.textContent = this.designName;
    if (statusEl) {
      if (this.isConnected) {
        statusEl.innerHTML = '<span style="color: #22c55e;">🟢 Conectado (ws://5000)</span>';
      } else {
        statusEl.innerHTML = '<span style="color: #ef4444;">🔴 Desconectado (Reintentando...)</span>';
      }
    }
    if (threadsEl) threadsEl.textContent = this.activeThreads;
  }

  connect() {
    const host = window.location.hostname || 'localhost';
    const wsUrl = `ws://${host}:5000`;
    console.log(`[NETWORK] Conectando a ${wsUrl}...`);

    try {
      this.socket = new WebSocket(wsUrl);

      this.socket.onopen = () => {
        console.log('[NETWORK] Conexión WebSocket establecida con éxito con el backend C++');
        this.isConnected = true;
        this.updateOverlay();
      };

      this.socket.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          if (data.design) this.designName = data.design;
          if (data.threads !== undefined) this.activeThreads = data.threads;
          if (data.cars) this.backendCars = data.cars;

          this.updateOverlay();
          this.notifyListeners(data);
        } catch (err) {
          console.error('[NETWORK] Error al parsear JSON del servidor:', err, event.data);
        }
      };

      this.socket.onclose = () => {
        if (this.isConnected) {
          console.warn('[NETWORK] Conexión cerrada por el servidor. Reintentando en 2s...');
        }
        this.isConnected = false;
        this.activeThreads = 0;
        this.updateOverlay();
        this.scheduleReconnect();
      };

      this.socket.onerror = (err) => {
        // En caso de error de conexión, se gestionará en onclose
        this.isConnected = false;
        this.updateOverlay();
      };
    } catch (e) {
      console.error('[NETWORK] Error al instanciar WebSocket:', e);
      this.scheduleReconnect();
    }
  }

  scheduleReconnect() {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, 2000);
  }

  addListener(callback) {
    this.listeners.push(callback);
  }

  notifyListeners(data) {
    for (const listener of this.listeners) {
      listener(data);
    }
  }
}

// Instancia global accesible para el juego
window.gameNetwork = new GameNetwork();
