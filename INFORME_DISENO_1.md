# Informe Académico: Diseño 1 - Hilos Independientes por Vehículo
**Materia:** Programación Paralela (300CIP013)  
**Profesor:** Jefferson Amado Peña  
**Semestre:** 2026-II  
**Institución:** Pontificia Universidad Javeriana Cali  
**Rama Git:** `diseno-1-hilos-independientes`

---

## 1. Descripción General y Modelo de Concurrencia

En el **Diseño 1**, se implementa una descomposición orientada a entidades o tareas completamente independientes. A cada vehículo enemigo que entra en la simulación se le asigna un hilo de ejecución exclusivo (`std::thread` sobre POSIX Threads en el backend en C++).

```text
               +----------------------------------------+
               |        Backend C++ (Servidor)          |
               +----------------------------------------+
                                   |
           +-----------------------+-----------------------+
           |                       |                       |
     [Thread Spawner]     [Thread Broadcaster]             |
           |                       |                       |
       Crea Carro 1                |                       |
           |                       |                       |
           v                       |                       |
  +------------------+             |                       |
  | Thread Carro 1   |             |                       |
  | ID: 1275279638...|             |                       |
  | y += speed       |             |                       |
  +------------------+             |                       |
           |                       |                       |
       Crea Carro 2                |                       |
           |                       |                       |
           v                       v                       v
  +------------------+   +-------------------+   +-------------------+
  | Thread Carro 2   |   | Recopila estado   |   | Envía vía Socket  |
  | ID: 1275279554...|-->| JSON con mutex    |-->| WebSocket RFC6455 |
  | y += speed       |   +-------------------+   +-------------------+
  +------------------+                                     |
                                                           v
                                              +-------------------------+
                                              | Frontend JS (Cliente)   |
                                              | PixiJS Canvas a 60 FPS  |
                                              +-------------------------+
```

### Ciclo de Vida del Hilo:
1. **Nacimiento:** El hilo generador (`spawnerThreadWorker`) detecta un carril libre en la parte superior y crea una instancia de `EnemyCar`. Inmediatamente lanza un `std::thread(carThreadWorker, newCar)`.
2. **Ejecución Asíncrona:** El hilo independiente ejecuta un bucle a ~50 Hz (`std::this_thread::sleep_for(20ms)`), incrementando la coordenada `y += speed` de forma autónoma.
3. **Muerte y Recolección:** Cuando `y > GAME_HEIGHT + 60`, el carro sale de la pantalla visible. El hilo marca `active = false` y finaliza su función. El hilo de transmisión (*Broadcaster* / *Reaper*) realiza un `join()` seguro sobre los hilos finalizados y los libera de la memoria.

---

## 2. Justificación de Decisiones Técnicas y Estructuras de Datos

### Estructuras de Datos Utilizadas
* `struct EnemyCar`: Contiene los atributos del carro (`id`, `type`, `laneIndex`, `x`, `y`, `speed`, `active`) y su hilo dedicado `std::thread workerThread`.
* `std::vector<std::shared_ptr<EnemyCar>> g_activeCars`: Contenedor dinámico en memoria compartida que almacena los punteros a los vehículos enemigos actualmente activos en la pista.
* `std::mutex g_carsMutex`: Cerrojo de exclusión mutua para proteger lecturas y escrituras sobre `g_activeCars`.
* `std::mutex g_socketMutex`: Cerrojo para evitar que múltiples hilos escriban tramas corruptas o intercaladas en el descriptor del socket.
* `std::atomic<bool> g_serverRunning`: Bandera atómica que asegura una detención limpia y sin condiciones de carrera.

### Prevención de Colisiones entre Enemigos
La especificación del proyecto estipula: *"Los vehículos enemigos no deberían compartir posición (no hay colisión entre enemigos)"*.
Para garantizarlo:
1. La pista cuenta con 6 posiciones discretas de carril: `X = {160, 224, 288, 352, 416, 480}`.
2. Al momento de generar un carro, la función `isLaneFreeAtTop(laneIdx)` recorre los vehículos activos bajo protección de `g_carsMutex`. Si en dicho carril existe un vehículo con `y < 180.0f` (aún en la zona superior), se prohíbe el spawn en ese carril y se selecciona uno verdaderamente libre.

### Comunicación Cliente-Servidor (WebSocket RFC 6455)
El servidor implementa de forma nativa el protocolo WebSocket, incluyendo el cálculo del hash SHA-1 y la codificación Base64 para el *handshake* HTTP 101. De este modo, la aplicación no requiere dependencias externas complejas, compilándose de forma limpia y portátil con `g++ server.cpp -o server -pthread`.

---

## 3. Respuestas a las Preguntas de la Guía de Evaluación

### 1. ¿Por qué este diseño puede o no aprovechar múltiples núcleos del procesador?
**Respuesta:**  
Este diseño **SÍ puede aprovechar múltiples núcleos del procesador**.  
Al utilizar `std::thread`, cada vehículo enemigo se mapea directamente a un hilo nativo del sistema operativo (hilos a nivel de kernel gestionados por la biblioteca `pthread` en Linux). El planificador (*scheduler*) del sistema operativo distribuye estos hilos entre los diferentes núcleos físicos y lógicos de la CPU. Por lo tanto, si la máquina cuenta con 4, 8 o 16 núcleos, las actualizaciones de posición de los carros se ejecutan en **verdadero paralelismo hardware**.  
*Matiz de rendimiento:* Debido a que la carga de trabajo de cada hilo es mínima (una suma aritmética `y += speed` y un sleep), para una cantidad pequeña de vehículos la ganancia de velocidad no es representativa en comparación con el costo del cambio de contexto (*context switching*).

### 2. ¿Qué tan fácil es agregar nuevos vehículos? ¿Qué nivel de independencia tiene cada vehículo?
**Respuesta:**  
* **Facilidad:** Es sumamente sencillo y modular. Agregar un nuevo vehículo requiere únicamente instanciar el objeto e invocar `std::thread(carThreadWorker, nuevoCarro)`. No es necesario recalcular particiones de datos ni redistribuir tareas en estructuras complejas.
* **Nivel de independencia:** Cada vehículo posee el **máximo nivel de independencia lógica**. Cada carro tiene su propio bucle de control, su propia frecuencia o temporización, su propia velocidad y su propio ciclo de vida. Ningún hilo de vehículo espera o bloquea a los demás vehículos durante su recorrido.

### 3. ¿Qué ocurre si existen miles de vehículos?
**Respuesta:**  
Ocurre un fenómeno crítico conocido como **Explosión de Hilos (*Thread Explosion*) y Saturación de Recursos**:
1. **Consumo excesivo de memoria:** Cada hilo del sistema operativo reserva por defecto una pila (*call stack*) de memoria (típicamente entre 1 MB y 8 MB en Linux). Crear 10,000 vehículos implicaría intentar reservar entre 10 GB y 80 GB de memoria solo para pilas de hilos, llevando a un error de `Out of Memory` (OOM) o fallo en `pthread_create`.
2. **Sobrecarga de Cambio de Contexto (*Context Switch Overhead*):** Si hay miles de hilos compitiendo por un número finito de núcleos (ej. 8 núcleos), la CPU pasa más tiempo guardando y restaurando registros de procesador y desalojando líneas de caché L1/L2/L3 que ejecutando el código real del juego.
3. **Agotamiento del límite del SO:** Se puede alcanzar el límite de procesos/hilos por usuario definido por el sistema (`ulimit -u`), provocando un bloqueo total del servidor.

### 4. ¿Incluyó variables compartidas entre hilos para el diseño? ¿Dónde podría ocurrir una condición de carrera?
**Respuesta:**  
**Sí, se incluyeron variables compartidas en memoria:**
1. El vector global `g_activeCars` donde se registran los vehículos activos.
2. El descriptor de socket `g_clientSocket`.
3. El contador global de identificadores `g_nextCarId`.

**Posibles condiciones de carrera y su mitigación:**
* **Condición de carrera 1:** Si el hilo Spawner inserta un carro (`push_back`) mientras el hilo Broadcaster recorre el vector para armar el JSON o eliminar carros inactivos, la estructura interna del `std::vector` se corrompe (punteros inválidos por realocación de memoria).  
  *Mitigación:* Se utilizó `std::lock_guard<std::mutex> lock(g_carsMutex)` en todos los puntos de acceso.
* **Condición de carrera 2:** Si múltiples hilos intentaran escribir simultáneamente en el socket de red, los paquetes TCP/WebSocket se intercalarían, enviando tramas corruptas que el frontend no podría decodificar.  
  *Mitigación:* Se implementó `g_socketMutex` para garantizar escrituras atómicas por trama.
* **Condición de carrera 3:** En la generación de IDs únicos, lecturas y escrituras simultáneas a un entero simple podrían generar identificadores duplicados.  
  *Mitigación:* Se utilizó `std::atomic<int> g_nextCarId` con la operación atómica `fetch_add(1)`.

### 5. ¿Qué estructuras de datos utilizó para cada diseño?
**Respuesta:**  
* `std::vector<std::shared_ptr<EnemyCar>>`: Para la colección dinámica de carros activos, permitiendo iteración secuencial rápida y administración automática de memoria mediante conteo de referencias.
* `std::thread`: Como objeto de control de ejecución concurrente para cada vehículo.
* `std::mutex`: Para la sincronización por exclusión mutua de las regiones críticas.
* `std::atomic<T>`: Para variables de control de estado y generación de claves libres de cerrojos (*lock-free*).
* Arreglo estático `int LANE_POSITIONS[6]`: Para la consulta rápida en $O(1)$ de las coordenadas horizontales de los carriles.

---

## 4. Matriz de Ventajas y Desventajas (Diseño 1)

| Criterio | Ventajas | Desventajas |
| :--- | :--- | :--- |
| **Arquitectura y Código** | Código muy intuitivo y conceptualmente directo. Mapeo 1:1 entre objeto del mundo real y unidad de ejecución. | Requiere un mecanismo de recolección (*reaper*) para hacer `join()` a los hilos terminados y no dejar hilos zombi. |
| **Rendimiento** | Aprovecha múltiples núcleos de CPU automáticamente sin necesidad de particionamiento manual. | Gran desperdicio de ciclos de CPU y memoria en cambios de contexto para tareas computacionalmente triviales. |
| **Escalabilidad** | Adecuado para simulaciones con pocos actores (ej. de 5 a 20 enemigos simultáneos). | **Pésima escalabilidad** ante cargas masivas (cientos o miles de enemigos) debido al agotamiento de recursos del sistema operativo. |
| **Mantenimiento** | Modificar el comportamiento de un carro no afecta el hilo de los demás carros. | Depurar condiciones de carrera en arquitecturas con decenas de hilos efímeros es complejo si no se protegen bien las estructuras compartidas. |

---

## 5. Instrucciones de Ejecución y Verificación

1. **Construir y levantar contenedores:**
   ```bash
   docker compose up --build -d
   ```
2. **Verificar logs del backend en tiempo real:**
   ```bash
   docker compose logs -f backend
   ```
   *Se observará cómo cada vehículo nuevo crea su propio Thread ID y finaliza al salir de la pantalla.*
3. **Abrir el cliente en el navegador:**
   Ingresar a [http://localhost:8080](http://localhost:8080).
   *En la esquina superior izquierda se observará el panel:*
   * **Diseño:** `Diseño 1: Hilos Independientes`
   * **Estado Socket:** `🟢 Conectado (ws://5000)`
   * **Hilos Activos (Carros):** Conteo dinámico en tiempo real.
