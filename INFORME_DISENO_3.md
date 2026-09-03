# Informe Académico: Diseño 3 - Hilo para Cada Tipo de Vehículo
**Materia:** Programación Paralela (300CIP013)  
**Profesor:** Jefferson Amado Peña  
**Semestre:** 2026-II  
**Institución:** Pontificia Universidad Javeriana Cali  
**Rama Git:** `diseno-3-hilos-por-tipo`

---

## 1. Descripción General y Modelo de Concurrencia

En el **Diseño 3**, la concurrencia se estructura mediante el patrón de **Descomposición por Dominio de Datos (*Domain Decomposition*)**. En lugar de asignar un hilo por vehículo (Diseño 1) o un único hilo para todos (Diseño 2), los vehículos se clasifican por una propiedad intrínseca: su **tipo o color de vehículo** (Rojo, Azul, Verde, Rosa y Blanco).

Se crean **5 hilos de actualización dedicados y permanentes**, donde cada hilo es el único responsable de procesar la lista de vehículos correspondiente a su categoría:

```text
               +----------------------------------------+
               |        Backend C++ (Servidor)          |
               +----------------------------------------+
                                   |
           +-----------------------+-----------------------+
           |                                               |
     [Thread Spawner]                             [Thread Broadcaster]
           |                                               |
   Clasifica por Tipo                               Recolecta todos
   e inserta en lista                               los carros (1..5)
           |                                               |
           +---------------+---------------+---------------+
           |               |               |               |
           v               v               v               v
    +-------------+ +-------------+ +-------------+ +-------------+
    | g_carsType1 | | g_carsType2 | | g_carsType3 | | g_carsType4 | ...
    +-------------+ +-------------+ +-------------+ +-------------+
           ^               ^               ^               ^
           |               |               |               |
    +-------------+ +-------------+ +-------------+ +-------------+
    | Hilo Tipo 1 | | Hilo Tipo 2 | | Hilo Tipo 3 | | Hilo Tipo 4 |
    | (Rojo)      | | (Azul)      | | (Verde)     | | (Rosa)      |
    +-------------+ +-------------+ +-------------+ +-------------+
    (Thread ID 1)   (Thread ID 2)   (Thread ID 3)   (Thread ID 4)
           |               |               |               |
           +---------------+---------------+---------------+
                                   |
                                   v
                         +-------------------+
                         | Envía vía Socket  |
                         | WebSocket RFC6455 |
                         +-------------------+
                                   |
                                   v
                      +-------------------------+
                      | Frontend JS (Cliente)   |
                      | PixiJS Canvas a 60 FPS  |
                      +-------------------------+
```

### Dinámica de Ejecución:
1. **Inicialización:** Al arrancar el servidor se lanzan los 5 hilos de tipo:
   ```cpp
   for (int t = 1; t <= 5; t++) {
       typeThreads.emplace_back(typeUpdateThreadWorker, t);
   }
   ```
2. **Cerrojos de grano fino (*Fine-Grained Locking*):** Cada tipo dispone de su propio vector `g_carsByType[t]` y su propio cerrojo `g_typeMutex[t]`. El Hilo 1 (Rojo) y el Hilo 2 (Azul) **no compiten por el mismo cerrojo**, lo que permite una ejecución verdaderamente paralela y sin contención en procesadores multinúcleo.
3. **Ciclo de Actualización:** Cada hilo actualiza sus carros a ~50 Hz, eliminando aquellos que salgan de la pantalla inferior (`y > GAME_HEIGHT + 60`).

---

## 2. Justificación de Decisiones Técnicas y Estructuras de Datos

### Estructuras de Datos Utilizadas
* `std::vector<std::shared_ptr<EnemyCar>> g_carsByType[6]`: Arreglo de 5 vectores dinámicos (índices 1 a 5), permitiendo que cada hilo trabaje sobre una partición independiente de memoria.
* `std::mutex g_typeMutex[6]`: Arreglo de 5 cerrojos de exclusión mutua independientes. Garantiza que la inserción de un carro rojo por el Spawner no bloquee la actualización de los carros azules o verdes.
* `std::mutex g_socketMutex`: Cerrojo que protege las transmisiones de red para evitar colisiones de paquetes en el descriptor del socket.
* `std::atomic<bool> g_serverRunning`: Bandera de sincronización atómica para la finalización controlada de los 5 hilos.

---

## 3. Respuestas a las Preguntas de la Guía de Evaluación

### 1. ¿Por qué este diseño puede o no aprovechar múltiples núcleos del procesador?
**Respuesta:**  
Este diseño **SÍ aprovecha múltiples núcleos del procesador**, hasta un límite máximo de $K = 5$ núcleos en paralelo para la simulación física (más los hilos de red y generación).  
Al existir 5 hilos de trabajo independientes (`std::thread`), el planificador del sistema operativo asigna cada hilo a núcleos distintos. Dado que cada hilo opera sobre su propio contenedor `g_carsByType[t]` protegido por su propio cerrojo `g_typeMutex[t]`, los 5 hilos se ejecutan de forma concurrente y paralela en hardware sin bloquearse mutuamente.  
*Limitación:* Si el procesador cuenta con más de 5 núcleos para cálculo de física (ej. 16 núcleos), los núcleos sobrantes no podrán ser aprovechados para la física porque el grado de paralelismo está estrictamente acotado por la cantidad de tipos de vehículos definidos ($K=5$).

### 2. ¿Qué tan fácil es agregar nuevos vehículos? ¿Qué nivel de independencia tiene cada vehículo?
**Respuesta:**  
* **Facilidad:** Es muy sencillo. El generador simplemente identifica el tipo de carro $t \in [1..5]$ y realiza una inserción directa en el vector correspondiente: `g_carsByType[t].push_back(newCar)` protegiendo únicamente `g_typeMutex[t]`.
* **Nivel de independencia:** La independencia es **intermedia (a nivel de categoría o dominio)**:
  * Carros de *diferente tipo* tienen **total independencia de ejecución** (la actualización de un carro rojo corre en paralelo y no depende de la velocidad ni cálculos de un carro verde).
  * Carros del *mismo tipo* están acoplados secuencialmente al hilo asignado a esa categoría.

### 3. ¿Qué ocurre si existen miles de vehículos?
**Respuesta:**  
El comportamiento ante miles de vehículos presenta dos fenómenos clave:
1. **Inmunidad a la Explosión de Hilos:** El número de hilos de trabajo es constante e invariante ($K = 5$). No hay riesgo de agotar la memoria del sistema por reserva masiva de pilas (*stacks*).
2. **Riesgo de Desbalance de Carga (*Load Imbalance*):** Si la distribución de vehículos por tipo no es uniforme (por ejemplo, si el generador produce un 80% de carros rojos y un 5% de los demás), el Hilo 1 (Rojo) se sobrecargará de trabajo superando los 20 ms del tick, mientras los otros 4 hilos permanecerán ociosos. La velocidad de la simulación se verá limitada por el hilo más cargado (*straggler thread*).

### 4. ¿Incluyó variables compartidas entre hilos para el diseño? ¿Dónde podría ocurrir una condición de carrera?
**Respuesta:**  
**Sí, se incluyeron variables compartidas particionadas:**
* Los vectores por tipo `g_carsByType[1..5]`.
* El socket del cliente `g_clientSocket`.

**Condiciones de carrera identificadas y su mitigación:**
* **Condición de carrera entre Spawner y el Hilo de Tipo:** Si el generador inserta un nuevo carro en `g_carsByType[t]` mientras el Hilo Tipo $t$ está iterando sobre ese mismo vector, se corrompen los iteradores.  
  *Mitigación:* Se protege el acceso con `std::lock_guard<std::mutex> lock(g_typeMutex[t])`. Al ser cerrojos independientes por tipo, el bloqueo dura una fracción mínima de tiempo y no interfiere con los demás tipos.
* **Condición de carrera en la recopilación del Broadcaster:** El hilo transmisor necesita consolidar las posiciones de los 5 tipos para enviar el JSON. Si leyera un vector mientras su hilo correspondiente modifica una posición, se generarían lecturas inconsistentes (*torn reads*).  
  *Mitigación:* El Broadcaster adquiere sucesivamente los cerrojos de cada tipo para tomar una instantánea segura.

### 5. ¿Qué estructuras de datos utilizó para cada diseño?
**Respuesta:**  
* Arreglo de vectores `std::vector<std::shared_ptr<EnemyCar>> g_carsByType[6]`: Estructura particionada para almacenar los carros según su categoría.
* Arreglo de cerrojos `std::mutex g_typeMutex[6]`: Cerrojos de grano fino asociados uno a uno a cada categoría.
* Vector de hilos `std::vector<std::thread> typeThreads`: Contenedor para administrar el ciclo de vida de los 5 hilos permanentes.
* `struct EnemyCar`: Entidad de datos físicos (posición, carril, tipo y velocidad).

---

## 4. Comparativa Tripartita: Diseños 1, 2 y 3

| Métrica / Criterio | Diseño 1 (Hilos Independientes) | Diseño 2 (Hilo Único) | Diseño 3 (Hilos por Tipo) |
| :--- | :--- | :--- | :--- |
| **Número de Hilos** | Dinámico ($N$ hilos, 1 por carro) | Fijo (1 hilo maestro) | Fijo ($K = 5$ hilos por categoría) |
| **Aprovechamiento de CPU** | Multinúcleo dinámico | Mononúcleo para física | Multinúcleo acotado (hasta 5 núcleos) |
| **Grano de Cerrojos** | Grano grueso global (`g_carsMutex`) | Grano grueso global (`g_carsMutex`) | **Grano fino por categoría (`g_typeMutex[t]`)** |
| **Contención de Cerrojos** | Alta con muchos carros | Baja (solo 3 hilos) | **Mínima** (hilos no compiten entre sí) |
| **Riesgo ante miles de carros** | *Thread Explosion* y colapso de RAM | Retraso en física (Frame Drops) | Desbalance de carga si un tipo predomina |
| **Complejidad de Código** | Moderada (requiere reaper) | Baja (secuencial simple) | Media-Alta (particionamiento y sincronización fina) |

---

## 5. Instrucciones de Verificación y Evidencias

1. **Reconstruir y desplegar en Docker:**
   ```bash
   docker compose build backend && docker compose up -d
   ```
2. **Comprobar registros en tiempo real:**
   ```bash
   docker compose logs -f backend
   ```
   *Salida esperada:*
   ```text
   [DISEÑO 3] Hilo Tipo 1 (Rojo) INICIADO (Thread ID: 131564645697216)
   [DISEÑO 3] Hilo Tipo 2 (Azul) INICIADO (Thread ID: 131564637304512)
   [DISEÑO 3] Hilo Tipo 3 (Verde) INICIADO (Thread ID: 131564628911808)
   [DISEÑO 3] Hilo Tipo 4 (Rosa) INICIADO (Thread ID: 131564620519104)
   [DISEÑO 3] Hilo Tipo 5 (Blanco) INICIADO (Thread ID: 131564612126400)
   [SPAWNER] Carro ID: 1 (Tipo: Verde) creado en carril 1 -> Asignado al Hilo Tipo 3
   [DISEÑO 3] Hilo Tipo 3 (Verde, Thread ID: 131564628911808) procesó 1 vehículos.
   [SPAWNER] Carro ID: 2 (Tipo: Rojo) creado en carril 4 -> Asignado al Hilo Tipo 1
   [DISEÑO 3] Hilo Tipo 1 (Rojo, Thread ID: 131564645697216) procesó 1 vehículos.
   ```
3. **Verificación en el frontend:**
   Abrir [http://localhost:8080](http://localhost:8080) (o Ctrl+F5).  
   El panel mostrará:
   * **Diseño:** `Diseño 3: Hilos por Tipo de Vehículo`
   * **Estado Socket:** `🟢 Conectado (ws://5000)`
   * **Hilos Activos (Tipos):** `5`
