# Informe Académico: Diseño 2 - Hilo Único de Actualización
**Materia:** Programación Paralela (300CIP013)  
**Profesor:** Jefferson Amado Peña  
**Semestre:** 2026-II  
**Institución:** Pontificia Universidad Javeriana Cali  
**Rama Git:** `diseno-2-hilo-unico`

---

## 1. Descripción General y Modelo de Concurrencia

En el **Diseño 2**, la arquitectura de actualización de la simulación cambia radicalmente respecto al Diseño 1. En lugar de asignar un hilo a cada vehículo, se centraliza la responsabilidad del movimiento en un **único hilo maestro de actualización** (`singleUpdateThreadWorker`).

```text
               +----------------------------------------+
               |        Backend C++ (Servidor)          |
               +----------------------------------------+
                                   |
           +-----------------------+-----------------------+
           |                       |                       |
     [Thread Spawner]      [Thread Broadcaster]            |
           |                       |                       |
       Inserta Carros              |                       |
       en vector global            |                       |
           |                       |                       |
           v                       |                       |
   +--------------------------------------------------+    |
   |        Memoria Compartida: g_activeCars          |    |
   |        [Carro 1, Carro 2, Carro 3, ... Car N]    |    |
   +--------------------------------------------------+    |
                           ^                               |
                           |                               |
       +---------------------------------------+           |
       |  HILO ÚNICO DE ACTUALIZACIÓN          |           |
       |  (singleUpdateThreadWorker)           |           |
       |  Thread ID Único                      |           |
       |  Itera for (auto& car : g_activeCars) |           |
       |  car->y += car->speed                 |           |
       +---------------------------------------+           |
                                   |                       v
                                   |             +-------------------+
                                   +------------>| Envía vía Socket  |
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
1. **Generación desacoplada:** El hilo *Spawner* crea instancias de `EnemyCar` en carriles libres superiores y las añade al vector compartido `g_activeCars`. **No se crea ningún hilo nuevo por carro.**
2. **Ciclo del Hilo Único:** Cada 20 ms (~50 Hz), el hilo de actualización adquiere el cerrojo de exclusión mutua (`g_carsMutex`) y recorre secuencialmente todos los vehículos activos, sumando su velocidad a la coordenada vertical:
   ```cpp
   for (auto& car : g_activeCars) {
       if (car->active) {
           car->y += car->speed;
           if (car->y > GAME_HEIGHT + 60.0f) {
               car->active = false;
           }
       }
   }
   ```
3. **Limpieza en el mismo ciclo:** Los carros que superan el límite inferior de la pantalla son eliminados directamente del vector por este mismo hilo, manteniendo la colección limpia y libre de fugas de memoria.

---

## 2. Justificación de Decisiones Técnicas y Estructuras de Datos

### Estructuras de Datos Utilizadas
* `struct EnemyCar`: Representación puramente de datos (`id`, `type`, `laneIndex`, `x`, `y`, `speed`, `active`). A diferencia del Diseño 1, ya no contiene ningún objeto `std::thread`, lo que reduce sustancialmente el tamaño en memoria de cada carro.
* `std::vector<std::shared_ptr<EnemyCar>> g_activeCars`: Contenedor contiguo en memoria. Para un hilo único que itera repetidamente sobre todos los elementos, el vector contiguo proporciona la **máxima eficiencia de caché L1/L2** (*spatial locality*).
* `std::mutex g_carsMutex`: Protege el vector contra accesos concurrentes entre los 3 hilos del servidor: *Spawner* (escritor de nuevos carros), *SingleUpdate* (escritor de posiciones y eliminador) y *Broadcaster* (lector para JSON).
* `std::mutex g_socketMutex`: Garantiza que el socket envíe tramas completas sin interferencias.

---

## 3. Respuestas a las Preguntas de la Guía de Evaluación

### 1. ¿Por qué este diseño puede o no aprovechar múltiples núcleos del procesador?
**Respuesta:**  
Este diseño **NO aprovecha múltiples núcleos del procesador para la simulación de los vehículos**.  
Aunque el backend posee hilos auxiliares para tareas de red (*Broadcaster*) y generación (*Spawner*), la tarea computacional principal (actualizar las posiciones de todos los vehículos) está contenida dentro de **un único hilo de ejecución**.  
Un hilo solo puede ejecutarse en un núcleo de CPU a la vez. En consecuencia, si la computadora tiene 8 o 16 núcleos físicos, solo 1 de ellos estará procesando el bucle de movimiento de los carros, mientras los demás núcleos permanecen desaprovechados para esta tarea.

### 2. ¿Qué tan fácil es agregar nuevos vehículos? ¿Qué nivel de independencia tiene cada vehículo?
**Respuesta:**  
* **Facilidad:** Es extraordinariamente fácil y rápido ($O(1)$ amortizado). Agregar un vehículo consiste únicamente en instanciar la estructura en memoria y hacer `g_activeCars.push_back(newCar)`. No existe sobrecarga de llamadas al sistema del kernel para crear hilos (`clone` / `pthread_create`).
* **Nivel de independencia:** Los vehículos **tienen un nivel muy bajo de independencia de ejecución**. No son entidades autónomas; su actualización está rígidamente acoplada dentro del mismo bucle for. Si el cálculo de un carro fuera costoso o se bloqueara, todos los demás vehículos de la simulación se quedarían congelados esperando a que termine.

### 3. ¿Qué ocurre si existen miles de vehículos?
**Respuesta:**  
El comportamiento ante miles de vehículos se caracteriza por:
1. **Sin explosión de hilos (Ventaja respecto al Diseño 1):** El consumo de memoria para pilas de ejecución es nulo adicionalmente, ya que solo existe un hilo de actualización sin importar si hay 10 o 100,000 carros.
2. **Caída drástica del rendimiento temporal (Frame Drops / Lag):** Para que el juego corra a 50 FPS constantes, el bucle debe terminar en menos de $20 \text{ ms}$. Si el número de carros $N$ crece a decenas de miles, el tiempo de iterar secuencialmente $O(N)$ sobrepasará la ventana de 20 ms. El hilo único no alcanzará a procesar a todos los carros a tiempo, acumulando retraso en la física y haciendo que el juego se ralentice de forma perceptible, sin posibilidad de aliviar la carga mediante núcleos adicionales.

### 4. ¿Incluyó variables compartidas entre hilos para el diseño? ¿Dónde podría ocurrir una condición de carrera?
**Respuesta:**  
**Sí, se incluyeron variables compartidas:**
* El vector global `g_activeCars`.
* El descriptor de socket `g_clientSocket`.

**Análisis de condiciones de carrera y mitigación:**
* *Entre carros:* A diferencia del Diseño 1, **no hay condición de carrera entre carros individuales** para actualizar sus posiciones, ya que un solo hilo los procesa uno tras otro en orden serial (patrón *single-writer*).
* *Entre hilos del servidor:* Puede ocurrir una condición de carrera crítica si el hilo *Spawner* inserta un carro (`push_back`) mientras el hilo *SingleUpdate* itera o mientras el hilo *Broadcaster* lee las posiciones para serializarlas a JSON. Una inserción simultánea causaría la realocación del vector en memoria, dejando a los otros hilos con punteros inválidos y provocando una falla de segmentación (*Segmentation Fault*).  
  *Mitigación:* El vector siempre se accede bajo el amparo de `std::lock_guard<std::mutex> lock(g_carsMutex)`.

### 5. ¿Qué estructuras de datos utilizó para cada diseño?
**Respuesta:**  
* `struct EnemyCar`: Estructura puramente de datos para representar el estado físico de cada carro.
* `std::vector<std::shared_ptr<EnemyCar>>`: Vector dinámico contiguo en memoria, ideal para iteraciones secuenciales rápidas con excelente rendimiento de caché.
* `std::mutex`: Para la sincronización de la región crítica compartida.
* `std::atomic<bool>`: Para la señalización de terminación limpia del bucle sin cerrojos.

---

## 4. Comparación: Diseño 1 (Hilos Independientes) vs. Diseño 2 (Hilo Único)

| Característica | Diseño 1 (Hilos Independientes) | Diseño 2 (Hilo Único) |
| :--- | :--- | :--- |
| **Hilos de actualización** | $N$ hilos (1 hilo por carro). | **1 hilo** para todos los carros. |
| **Uso de múltiples núcleos** | Sí, el SO distribuye los hilos en varios núcleos. | No, todo el movimiento corre en 1 solo núcleo. |
| **Sobrecarga de creación** | Alta (`pthread_create` por carro). | Mínima (solo inserción en vector $O(1)$). |
| **Consumo de memoria** | Muy alto (reserva de stack de 1-8 MB por hilo). | Muy bajo (solo la memoria del struct). |
| **Comportamiento ante miles de carros** | Catastrófico: *Thread Explosion* y bloqueo del SO. | Estable en memoria, pero con retraso temporal en física (*lag*). |
| **Complejidad de sincronización** | Alta (muchos hilos accediendo a memoria). | Moderada (solo 3 hilos coordinados con mutex). |

---

## 5. Instrucciones de Verificación y Evidencias

1. **Reconstruir y desplegar en Docker:**
   ```bash
   docker compose build backend && docker compose up -d
   ```
2. **Monitorear los registros del servidor:**
   ```bash
   docker compose logs -f backend
   ```
   *Salida esperada:*
   ```text
   [DISEÑO 2] Hilo Único de Actualización INICIADO (Thread ID: 128276016445120)
   [SPAWNER] Carro ID: 1 creado en carril 3 (Delegado al Hilo Único de Actualización)
   [DISEÑO 2] Hilo Único (Thread ID: 128276016445120) procesó secuencialmente 1 vehículos.
   [SPAWNER] Carro ID: 2 creado en carril 5 (Delegado al Hilo Único de Actualización)
   [DISEÑO 2] Hilo Único (Thread ID: 128276016445120) procesó secuencialmente 2 vehículos.
   ```
3. **Comprobar en el frontend:**
   Abrir [http://localhost:8080](http://localhost:8080) (o recargar con Ctrl+F5).  
   El panel mostrará:
   * **Diseño:** `Diseño 2: Hilo Único de Actualización`
   * **Estado Socket:** `🟢 Conectado (ws://5000)`
   * **Hilos Activos:** `1`
