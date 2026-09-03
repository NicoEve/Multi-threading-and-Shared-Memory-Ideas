# Informe Académico: Diseño 4 - Hilo Asíncrono para Vehículos (Thread Pool y Cola de Tareas)
**Materia:** Programación Paralela (300CIP013)  
**Profesor:** Jefferson Amado Peña  
**Semestre:** 2026-II  
**Institución:** Pontificia Universidad Javeriana Cali  
**Rama Git:** `diseno-4-pool-asincrono`

---

## 1. Descripción General y Modelo de Concurrencia

En el **Diseño 4**, se implementa el patrón de concurrencia industrial por excelencia: el **Grupo de Hilos de Trabajo con Cola de Tareas (*Worker Thread Pool Pattern*)**.

Bajo este modelo, **los vehículos no tienen un hilo permanente asignado**. La actualización del movimiento de un vehículo se concibe como una **tarea atómica y asíncrona** (`CarUpdateTask`). Existe un número fijo y predeterminado de hilos trabajadores ($M = 4$ workers, dimensionado para coincidir con la cantidad de núcleos físicos típicos de la CPU), los cuales extraen dinámicamente tareas desde una **cola de tareas sincronizada y segura** (*thread-safe task queue*):

```text
               +----------------------------------------+
               |        Backend C++ (Servidor)          |
               +----------------------------------------+
                                   |
           +-----------------------+-----------------------+
           |                                               |
     [Thread Spawner]                             [Thread Broadcaster]
           |                                               |
       Crea Carros                                  Serializa JSON y
     en memoria compartida                           envía por Socket
           |                                               |
           v                                               v
   +---------------------------------------------------------------+
   |                      HILO DESPACHADOR                         |
   |              (dispatcherThreadWorker - 50 Hz)                 |
   |   Itera carros activos y ENCOLA una tarea para cada uno       |
   +---------------------------------------------------------------+
                                   |
                                   v
             +-------------------------------------------+
             |         COLA DE TAREAS CONCURRENTE        |
             |             (std::queue<Task>)            |
             |   [ Tarea Carro 1 | Tarea 2 | Tarea 3 ...]|
             +-------------------------------------------+
                     |             |             |
                     v             v             v
             +---------------+---------------+---------------+
             |   Worker 1    |   Worker 2    |   Worker 3    |  ... (M=4)
             | (Thread ID 1) | (Thread ID 2) | (Thread ID 3) |
             +---------------+---------------+---------------+
               Esperan en condition_variable, extraen tareas
               y actualizan car->y += speed concurrentemente
```

### Dinámica de Ejecución:
1. **Piscina fija de hilos (*Thread Pool*):** Se instancian exactamente $M = 4$ hilos al arrancar el servidor. Los hilos entran en un estado de espera pasiva y eficiente en CPU utilizando variables de condición (`std::condition_variable::wait`).
2. **Generación de tareas:** En cada tick de la simulación (~20 ms), el despachador recorre los vehículos activos y deposita una `CarUpdateTask` por cada carro en `g_taskQueue`, notificando a los trabajadores mediante `g_queueCV.notify_all()`.
3. **Consumo dinámico y balanceo de carga:** Cualquier hilo libre del pool se despierta, extrae una tarea de la cola, calcula la nueva posición del carro (`car->y += car->speed`) y queda disponible de inmediato para la siguiente tarea.
4. **Desacoplamiento total:** Un mismo carro puede ser actualizado por el Worker 2 en el tick 1, por el Worker 4 en el tick 2 y por el Worker 1 en el tick 3.

---

## 2. Evaluación de una Implementación SIN WebSockets

La guía del proyecto estipula expresamente: *(evalúe una implementación sin websockets)*. A continuación se presenta el análisis técnico y comparativo de las alternativas viables:

### Alternativa 1: HTTP REST con Polling Corto (*Short Polling*)
* **Mecanismo:** El navegador realiza peticiones periódicas mediante `fetch()` (`GET /api/cars`) cada 20 ms.
* **Sobrecarga de Red (*Protocol Overhead*):** Enorme. Cada petición HTTP transporta entre 500 y 1,000 bytes de cabeceras redundantes (User-Agent, Cookies, Accept, etc.). A 50 peticiones por segundo, esto genera entre **25 KB/s y 50 KB/s de tráfico inútil por cada cliente conectado**, frente a los escasos **2 a 4 bytes de cabecera** de una trama WebSocket.
* **Sobrecarga en el Servidor:** Abrir y cerrar conexiones TCP a alta frecuencia agota los puertos efímeros del sistema operativo (*TIME_WAIT socket exhaustion*).

### Alternativa 2: HTTP con Polling Largo (*Long Polling*)
* **Mecanismo:** El cliente abre una petición HTTP y el servidor la mantiene en suspensión hasta que haya una actualización de datos.
* **Evaluación en Simulación de Tiempo Real:** En un juego donde los vehículos se mueven continuamente cada 20 ms, **siempre hay datos nuevos en cada tick**. Por lo tanto, el Long Polling degenera inmediatamente en Short Polling, heredando todas sus desventajas sin aportar ningún beneficio.

### Alternativa 3: Server-Sent Events (SSE - `EventSource`)
* **Mecanismo:** Se abre una conexión HTTP persistente unidireccional donde el servidor envía flujos de texto continuo (`text/event-stream`).
* **Ventajas:** Es ligera, viaja sobre HTTP estándar sin necesidad de apretón de manos binario y es soportada de forma nativa por los navegadores.
* **Desventajas:** Es estrictamente **unidireccional** (servidor hacia cliente). Si el cliente necesita enviar datos (como comandos de pausa, reinicio o cambio de dificultad), debe abrir peticiones HTTP POST secundarias, fragmentando la arquitectura.

### Alternativa 4: Sockets TCP Crudos (*Raw Berkeley Sockets*)
* **Mecanismo:** Envío de estructuras binarias directas mediante `send()` y `recv()`.
* **Evaluación:** Es la opción más rápida posible y la de menor consumo de CPU en sistemas nativos. Sin embargo, **los navegadores web modernos prohíben por motivos de seguridad el acceso a sockets TCP crudos en JavaScript**. Solo es viable si el cliente fuera una aplicación de escritorio en C++, Java o Python.

### Conclusión de la Evaluación
**WebSocket es la solución óptima e idónea** para simulaciones interactivas en tiempo real basadas en navegador. Ofrece la latencia mínima y bidireccionalidad de un socket TCP nativo, atravesando firewalls y proxies mediante el puerto HTTP estándar con una sobrecarga casi nula (2 bytes de framing).

---

## 3. Respuestas a las Preguntas de la Guía de Evaluación

### 1. ¿Por qué este diseño puede o no aprovechar múltiples núcleos del procesador?
**Respuesta:**  
Este diseño **SÍ aprovecha al máximo y de forma óptima los múltiples núcleos del procesador**.  
Al fijar el tamaño del pool a $M = 4$ hilos trabajadores, el sistema operativo distribuye exactamente un hilo en cada uno de los núcleos físicos de la CPU. Dado que las tareas se extraen de la cola de forma dinámica, todos los núcleos se mantienen ocupados computando tareas de actualización concurrentemente mientras haya carros en la cola. A diferencia del Diseño 3 (donde un núcleo podía quedar ocioso si no había carros de su color), en el Diseño 4 cualquier núcleo disponible toma el siguiente carro en espera, logrando un **balanceo de carga dinámico perfecto**.

### 2. ¿Qué tan fácil es agregar nuevos vehículos? ¿Qué nivel de independencia tiene cada vehículo?
**Respuesta:**  
* **Facilidad:** Es completamente transparente y trivial. Crear un vehículo consiste en agregarlo al vector de carros activos. El despachador se encarga de generar su tarea y depositarla en la cola de forma automática.
* **Nivel de independencia:** Los vehículos poseen **independencia funcional por tareas**. El ciclo de vida de un vehículo no está atado a la existencia ni a la duración de ningún hilo del sistema operativo. Si la simulación pasa de 1 carro a 500 carros, la estructura interna del sistema de hilos no sufre ninguna mutación.

### 3. ¿Qué ocurre si existen miles de vehículos?
**Respuesta:**  
El Diseño 4 es **LA SOLUCIÓN MÁS ROBUSTA, ESCALABLE Y ESTABLE** ante cargas masivas:
1. **Cero riesgo de explosión de hilos (*Thread Explosion*):** A diferencia del Diseño 1, si hay 10,000 vehículos, no se crean 10,000 hilos. La cantidad de hilos se mantiene rigurosamente constante en $M = 4$. El consumo de memoria para pilas de ejecución permanece invariable en unos pocos kilobytes.
2. **Resistencia a fallos y amortiguación (*Backpressure*):** La cola de tareas actúa como un búfer amortiguador (*buffer*). Si la cantidad de carros es tan masiva que el pool tarda más de 20 ms en procesarlos, las tareas simplemente esperan en la cola de forma ordenada sin saturar al planificador del sistema operativo con cambios de contexto masivos.

### 4. ¿Incluyó variables compartidas entre hilos para el diseño? ¿Dónde podría ocurrir una condición de carrera?
**Respuesta:**  
**Sí, se incluyeron variables compartidas críticas:**
* La cola de tareas `std::queue<CarUpdateTask> g_taskQueue`.
* El vector global de carros activos `g_activeCars`.
* El socket del cliente `g_clientSocket`.

**Análisis de condiciones de carrera y sincronización:**
* **Condición de carrera en la cola de tareas:** Múltiples workers intentando extraer tareas simultáneamente (`pop()`) o el despachador intentando insertar tareas (`push()`) al mismo tiempo provocarían la corrupción de los nodos de la cola.  
  *Mitigación:* Se implementó `std::mutex g_queueMutex` junto con `std::unique_lock` y `std::condition_variable g_queueCV`. Esto asegura que la extracción e inserción sean operaciones estrictamente atómicas y que los hilos no consuman ciclos de CPU mientras esperan tareas (*busy waiting* eliminado).
* **Condición de carrera en el estado del carro:** Si dos workers intentaran procesar la tarea del mismo carro al mismo tiempo.  
  *Mitigación:* En cada tick del despachador, se emite exactamente una única tarea por carro activo, garantizando que un carro nunca sea procesado por dos workers simultáneamente.

### 5. ¿Qué estructuras de datos utilizó para cada diseño?
**Respuesta:**  
* `std::queue<CarUpdateTask>`: Cola FIFO para el almacenamiento y despacho seguro de tareas concurrentes.
* `std::condition_variable`: Mecanismo de sincronización y señalización para dormir y despertar a los workers sin consumo de CPU.
* `std::vector<std::thread>`: Arreglo para albergar y gestionar el grupo fijo de hilos del Thread Pool.
* `std::unique_lock<std::mutex>`: Cerrojo avanzado requerido para operar con variables de condición.
* `struct EnemyCar` y `struct CarUpdateTask`: Entidades de datos y tareas de actualización desacopladas.

---

## 4. Matriz Comparativa General de los 4 Diseños del Micro-Proyecto

| Criterio de Evaluación | Diseño 1: Hilos Independientes | Diseño 2: Hilo Único | Diseño 3: Hilos por Tipo | Diseño 4: Pool Asíncrono |
| :--- | :--- | :--- | :--- | :--- |
| **Arquitectura de Hilos** | 1 hilo dedicado por cada vehículo | 1 solo hilo maestro para todos | 5 hilos dedicados (1 por color) | **Pool fijo de 4 workers con cola** |
| **Aprovechamiento Multinúcleo** | Sí, dinámico por el SO | No (mononúcleo para física) | Sí (hasta 5 núcleos acotados) | **Sí, óptimo y balanceado dinámicamente** |
| **Sincronización** | `std::mutex` global | `std::mutex` global | Cerrojos de grano fino por tipo | **`condition_variable` + `std::queue`** |
| **Impacto con Miles de Carros** | Catastrófico (*Thread Explosion*, colapso) | Pérdida de FPS / Lag temporal | Riesgo de desbalance de carga | **Escalabilidad óptima y memoria constante** |
| **Eficiencia de Memoria** | Muy baja (stacks masivos) | Muy alta (sin stacks extras) | Alta (solo 5 stacks fijos) | **Óptima (solo 4 stacks fijos)** |
| **Complejidad del Código** | Moderada | Baja | Media-Alta | Alta (patrón industrial) |

---

## 5. Instrucciones de Verificación y Evidencias

1. **Reconstruir y desplegar en Docker:**
   ```bash
   docker compose build backend && docker compose up -d
   ```
2. **Inspeccionar los logs del Thread Pool en tiempo real:**
   ```bash
   docker compose logs -f backend
   ```
   *Salida demostrativa del Thread Pool procesando tareas dinámicamente:*
   ```text
   [DISEÑO 4] Worker 1 del Pool INICIADO (Thread ID: 125450388068032)
   [DISEÑO 4] Worker 2 del Pool INICIADO (Thread ID: 125450379675328)
   [DISEÑO 4] Worker 3 del Pool INICIADO (Thread ID: 125450371282624)
   [DISEÑO 4] Worker 4 del Pool INICIADO (Thread ID: 125450362889920)
   [DISPATCHER] Hilo despachador de tareas iniciado.
   [SPAWNER] Carro ID: 1 creado -> Tareas asignadas al Thread Pool (sin hilo permanente)
   [DISEÑO 4] Worker 2 (Thread ID: 125450379675328) procesó tarea del Carro ID 1
   [DISEÑO 4] Worker 3 (Thread ID: 125450371282624) procesó tarea del Carro ID 2
   [DISEÑO 4] Worker 1 (Thread ID: 125450388068032) procesó tarea del Carro ID 3
   [DISEÑO 4] Worker 4 (Thread ID: 125450362889920) procesó tarea del Carro ID 1
   ```
3. **Verificación en el frontend:**
   Abrir [http://localhost:8080](http://localhost:8080) (o recargar con Ctrl+F5).  
   El panel mostrará:
   * **Diseño:** `Diseño 4: Hilo Asíncrono (Thread Pool)`
   * **Estado Socket:** `🟢 Conectado (ws://5000)`
   * **Hilos Activos (Workers):** `4`
