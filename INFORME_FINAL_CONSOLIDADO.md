# INFORME CONSOLIDADO DE LABORATORIO
## Micro-Proyecto No. 1: Multi-threading and Shared-Memory Ideas
**Evaluación, Implementación y Análisis Comparativo de 4 Diseños Multihilo en C++ con WebSockets**

---

### Datos de Identificación Institucional y Académica
* **Institución:** Pontificia Universidad Javeriana Cali
* **Facultad:** Facultad de Ingeniería y Ciencias
* **Departamento:** Departamento de Electrónica y Ciencias de la Computación
* **Asignatura:** Programación Paralela (300CIP013) — Periodo 2026-II
* **Profesor:** Jefferson Amado Peña
* **Estudiante (Autor):** Nicolás Zapata Clavijo
* **Código Estudiantil:** 8984273
* **Fecha de Entrega:** Septiembre de 2026
* **Repositorio Base:** [https://github.com/NicoEve/Multi-threading-and-Shared-Memory-Ideas](https://github.com/NicoEve/Multi-threading-and-Shared-Memory-Ideas)
* **Enlace al Video de Sustentación:** `[Insertar enlace de YouTube / OneDrive / Stream aquí]`

---

### Enlaces Directos a las 4 Ramas del Repositorio
1. **Diseño 1 (Hilos Independientes):** [Rama `diseno-1-hilos-independientes`](https://github.com/NicoEve/Multi-threading-and-Shared-Memory-Ideas/tree/diseno-1-hilos-independientes)
2. **Diseño 2 (Hilo Único):** [Rama `diseno-2-hilo-unico`](https://github.com/NicoEve/Multi-threading-and-Shared-Memory-Ideas/tree/diseno-2-hilo-unico)
3. **Diseño 3 (Hilos por Tipo de Vehículo):** [Rama `diseno-3-hilos-por-tipo`](https://github.com/NicoEve/Multi-threading-and-Shared-Memory-Ideas/tree/diseno-3-hilos-por-tipo)
4. **Diseño 4 (Thread Pool Asíncrono):** [Rama `diseno-4-pool-asincrono`](https://github.com/NicoEve/Multi-threading-and-Shared-Memory-Ideas/tree/diseno-4-pool-asincrono)

---

## 1. Introducción y Arquitectura del Sistema

El objetivo de este micro-proyecto es aplicar de manera práctica los conceptos fundamentales de concurrencia y memoria compartida en C++ (`std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`) en un entorno cliente-servidor para el juego *Enemy Cars*.

* **Backend (C++ Servidor):** Responsable exclusivo de la física de los vehículos enemigos: generación, actualización de coordenadas a ~50 Hz y transmisión de estados.
* **Frontend (HTML5 Canvas / PixiJS):** Servido por Nginx en el puerto `8080`, recibe las posiciones emitidas por el backend mediante un cliente WebSocket y las renderiza en pantalla a 60 FPS, manteniendo la detección de colisiones con el jugador, sonidos y puntuación.
* **Protocolo WebSocket RFC 6455 Nativo:** Implementado en `server.cpp` con algoritmos nativos de SHA-1 y Base64, evitando dependencias externas complejas y garantizando una compilación limpia en Docker (`g++ server.cpp -o server -pthread`).
* **Prevención de Colisiones entre Enemigos:** Discretización de 6 carriles horizontales (`X = 160, 224, 288, 352, 416, 480`). Antes del spawn, la función `isLaneFreeAtTop` verifica bajo mutex que no exista otro vehículo a menos de 180 píxeles, garantizando que los enemigos no colisionen entre sí.

---

## 2. Diseño 1: Hilos Independientes por Vehículo
**Rama:** `diseno-1-hilos-independientes` | **Modelo:** 1 Vehículo = 1 Hilo Nativo

### Descripción Técnica
Cada vehículo enemigo posee un hilo nativo (`std::thread`) dedicado a actualizar su coordenada `y += speed`. El hilo concluye cuando el carro sale de pantalla (`y > GAME_HEIGHT + 60`). El hilo transmisor realiza un `join()` seguro para recolectar recursos.

### Evidencia de Verificación en Docker
```text
[SERVIDOR] Escuchando en puerto 5000...
[CONEXIÓN] Cliente conectado desde 172.19.0.1
[DISEÑO 1] Hilo CREADO -> Carro ID: 1 | Carril: 0 | Vel: 2.9 | Thread ID: 127527963825856
[DISEÑO 1] Hilo CREADO -> Carro ID: 2 | Carril: 2 | Vel: 4.4 | Thread ID: 127527955433152
[DISEÑO 1] Hilo CREADO -> Carro ID: 3 | Carril: 3 | Vel: 4.4 | Thread ID: 127527947040448
[DISEÑO 1] Hilo CREADO -> Carro ID: 4 | Carril: 5 | Vel: 3.7 | Thread ID: 127527938647744
[DISEÑO 1] Hilo FINALIZADO -> Carro ID: 2 salió de pantalla (Thread ID: 127527955433152)
[DISEÑO 1] Hilo FINALIZADO -> Carro ID: 1 salió de pantalla (Thread ID: 127527963825856)
```

### Respuestas a las 5 Preguntas (Diseño 1)
1. **¿Por qué este diseño puede o no aprovechar múltiples núcleos del procesador?**  
   *Respuesta:* Sí lo aprovecha. Cada carro tiene un hilo nativo mapeado por el kernel a diferentes núcleos físicos. Sin embargo, para sumas matemáticas simples, el costo del cambio de contexto (*context switching*) amortigua las ganancias.
2. **¿Qué tan fácil es agregar nuevos vehículos? ¿Qué nivel de independencia tiene cada vehículo?**  
   *Respuesta:* Muy fácil: solo se instancia `new std::thread`. Cada vehículo posee máxima independencia lógica: su ciclo de vida y velocidad son autónomos.
3. **¿Qué ocurre si existen miles de vehículos?**  
   *Respuesta:* Ocurre **Explosión de Hilos (*Thread Explosion*)**. Con 10,000 vehículos se reservarían gigabytes de RAM solo en pilas de hilos (1-8 MB por hilo), colapsando el sistema por OOM o límite de `pthread_create`.
4. **¿Incluyó variables compartidas entre hilos para el diseño? ¿Dónde podría ocurrir una condición de carrera?**  
   *Respuesta:* Sí: el vector `g_activeCars`, el socket y el contador de IDs. Si el generador hace `push_back` mientras el transmisor lee, se corrompe la memoria. Se mitigó con `std::lock_guard<std::mutex>`.
5. **¿Qué estructuras de datos utilizó para cada diseño?**  
   *Respuesta:* `struct EnemyCar` con `std::thread`, `std::vector`, `std::mutex`, `std::atomic<int>`.

---

## 3. Diseño 2: Hilo Único de Actualización
**Rama:** `diseno-2-hilo-unico` | **Modelo:** 1 Hilo Maestro para Todos los Carros

### Descripción Técnica
Los vehículos son solo estructuras de datos en memoria. Un único hilo maestro (`singleUpdateThreadWorker`) itera secuencialmente todos los carros en cada tick de 20 ms bajo el patrón *single-writer*.

### Evidencia de Verificación en Docker
```text
[SERVIDOR] Escuchando en puerto 5000...
[DISEÑO 2] Hilo Único de Actualización INICIADO (Thread ID: 128276016445120)
[CONEXIÓN] Cliente conectado desde 172.19.0.1
[SPAWNER] Carro ID: 1 creado en carril 3 (Delegado al Hilo Único de Actualización)
[DISEÑO 2] Hilo Único (Thread ID: 128276016445120) procesó secuencialmente 1 vehículos.
[SPAWNER] Carro ID: 2 creado en carril 5 (Delegado al Hilo Único de Actualización)
[DISEÑO 2] Hilo Único (Thread ID: 128276016445120) procesó secuencialmente 2 vehículos.
[SPAWNER] Carro ID: 3 creado en carril 4 (Delegado al Hilo Único de Actualización)
[DISEÑO 2] Hilo Único (Thread ID: 128276016445120) procesó secuencialmente 3 vehículos.
```

### Respuestas a las 5 Preguntas (Diseño 2)
1. **¿Por qué este diseño puede o no aprovechar múltiples núcleos del procesador?**  
   *Respuesta:* **NO aprovecha múltiples núcleos para la física**. Un solo hilo corre en 1 solo núcleo de CPU a la vez; los demás núcleos quedan ociosos para el movimiento.
2. **¿Qué tan fácil es agregar nuevos vehículos? ¿Qué nivel de independencia tiene cada vehículo?**  
   *Respuesta:* Facilísimo ($O(1)$ inserción en vector sin sobrecarga de hilos). Cero independencia de ejecución: todos los carros están rígidamente acoplados en el mismo bucle serial.
3. **¿Qué ocurre si existen miles de vehículos?**  
   *Respuesta:* La memoria permanece estable (solo 1 hilo). Sin embargo, recorrer miles de carros excede la ventana de 20 ms del tick, provocando retraso en la física y caída de FPS.
4. **¿Incluyó variables compartidas entre hilos para el diseño? ¿Dónde podría ocurrir una condición de carrera?**  
   *Respuesta:* Sí: concurrencia entre Spawner, Hilo Único y Broadcaster; protegida con `std::mutex g_carsMutex`.
5. **¿Qué estructuras de datos utilizó para cada diseño?**  
   *Respuesta:* `struct EnemyCar` ligero, `std::vector` contiguo (máxima localidad espacial de caché), `std::mutex`.

---

## 4. Diseño 3: Hilo para Cada Tipo de Vehículo
**Rama:** `diseno-3-hilos-por-tipo` | **Modelo:** Descomposición por Dominio (5 Hilos por Color)

### Descripción Técnica
Existen 5 hilos de actualización dedicados (uno para Rojo, Azul, Verde, Rosa y Blanco). Se implementaron **cerrojos de grano fino (*Fine-Grained Locking*)**: cada tipo tiene su propio vector `g_carsByType[t]` y su propio cerrojo `g_typeMutex[t]`, de modo que los hilos no compiten entre sí.

### Evidencia de Verificación en Docker
```text
[SERVIDOR] Escuchando en puerto 5000...
[DISEÑO 3] Hilo Tipo 1 (Rojo) INICIADO (Thread ID: 131564645697216)
[DISEÑO 3] Hilo Tipo 2 (Azul) INICIADO (Thread ID: 131564637304512)
[DISEÑO 3] Hilo Tipo 3 (Verde) INICIADO (Thread ID: 131564628911808)
[DISEÑO 3] Hilo Tipo 4 (Rosa) INICIADO (Thread ID: 131564620519104)
[DISEÑO 3] Hilo Tipo 5 (Blanco) INICIADO (Thread ID: 131564612126400)
[SPAWNER] Carro ID: 1 (Tipo: Verde) creado -> Asignado al Hilo Tipo 3
[DISEÑO 3] Hilo Tipo 3 (Verde) procesó 1 vehículos.
[SPAWNER] Carro ID: 2 (Tipo: Rojo) creado -> Asignado al Hilo Tipo 1
[DISEÑO 3] Hilo Tipo 1 (Rojo) procesó 1 vehículos.
```

### Respuestas a las 5 Preguntas (Diseño 3)
1. **¿Por qué este diseño puede o no aprovechar múltiples núcleos del procesador?**  
   *Respuesta:* Sí lo aprovecha, hasta un límite acotado de $K = 5$ núcleos en paralelo real sin contención de cerrojos.
2. **¿Qué tan fácil es agregar nuevos vehículos? ¿Qué nivel de independencia tiene cada vehículo?**  
   *Respuesta:* Muy fácil ($O(1)$ en el vector de su tipo). Independencia intermedia: carros de distinto color no se interfieren, pero carros del mismo color comparten hilo.
3. **¿Qué ocurre si existen miles de vehículos?**  
   *Respuesta:* No hay explosión de hilos (siempre son 5). Sin embargo, es vulnerable a **Desbalance de Carga (*Load Imbalance*)** si un color concentra la mayoría de los vehículos.
4. **¿Incluyó variables compartidas entre hilos para el diseño? ¿Dónde podría ocurrir una condición de carrera?**  
   *Respuesta:* Sí: 5 vectores independientes protegidos por 5 mutexes independientes `g_typeMutex[1..5]`.
5. **¿Qué estructuras de datos utilizó para cada diseño?**  
   *Respuesta:* Arreglo de vectores `g_carsByType[6]`, arreglo de cerrojos `g_typeMutex[6]`, `std::vector<std::thread>`.

---

## 5. Diseño 4: Hilo Asíncrono para Vehículos (Thread Pool y Cola de Tareas)
**Rama:** `diseno-4-pool-asincrono` | **Modelo:** Worker Thread Pool (4 Workers) con Cola Concurrente

### Descripción Técnica
Los carros no tienen hilos permanentes. Existe un pool fijo de 4 trabajadores. En cada tick, el despachador encola tareas en `std::queue<CarUpdateTask>` y notifica a los workers mediante `std::condition_variable`. Los workers disponibles extraen tareas y actualizan los carros dinámicamente.

### Evidencia de Verificación en Docker
```text
[DISEÑO 4] Worker 1 del Pool INICIADO (Thread ID: 125450388068032)
[DISEÑO 4] Worker 2 del Pool INICIADO (Thread ID: 125450379675328)
[DISEÑO 4] Worker 3 del Pool INICIADO (Thread ID: 125450371282624)
[DISEÑO 4] Worker 4 del Pool INICIADO (Thread ID: 125450362889920)
[DISPATCHER] Hilo despachador de tareas iniciado.
[SPAWNER] Carro ID: 1 creado -> Tareas asignadas al Thread Pool (sin hilo permanente)
[DISEÑO 4] Worker 2 (Thread ID: ...328) procesó tarea del Carro ID 1
[DISEÑO 4] Worker 3 (Thread ID: ...624) procesó tarea del Carro ID 2
[DISEÑO 4] Worker 1 (Thread ID: ...032) procesó tarea del Carro ID 3
[DISEÑO 4] Worker 4 (Thread ID: ...920) procesó tarea del Carro ID 1
```

### Evaluación de una Implementación SIN WebSockets
* **HTTP Short Polling:** Inviable a 50 Hz. Incurre en 25-50 KB/s de sobrecarga por cliente en cabeceras HTTP de 1 KB repetidas y satura puertos en estado *TIME_WAIT*.
* **HTTP Long Polling:** En un juego en tiempo real donde la física cambia continuamente cada 20 ms, degenera en Short Polling sin ventaja alguna.
* **Server-Sent Events (SSE):** Ligero y de baja latencia, pero unidireccional (no permite enviar comandos del cliente por el mismo socket).
* **TCP Crudo:** Ideal para escritorio, pero bloqueado en navegadores web por seguridad de JavaScript.
* **Conclusión:** WebSocket es la opción óptima para juegos web en tiempo real.

### Respuestas a las 5 Preguntas (Diseño 4)
1. **¿Por qué este diseño puede o no aprovechar múltiples núcleos del procesador?**  
   *Respuesta:* Sí, de forma óptima. Los 4 workers corren en núcleos físicos distintos y toman tareas dinámicamente, asegurando que ningún núcleo quede ocioso.
2. **¿Qué tan fácil es agregar nuevos vehículos? ¿Qué nivel de independencia tiene cada vehículo?**  
   *Respuesta:* Muy fácil: independencia funcional por tareas. Los carros no están atados a hilos del sistema operativo.
3. **¿Qué ocurre si existen miles de vehículos?**  
   *Respuesta:* **Es el diseño más escalable**. La memoria es rigurosamente constante (4 workers) y la cola amortigua la carga (*backpressure*) sin saturar la CPU con cambios de contexto.
4. **¿Incluyó variables compartidas entre hilos para el diseño? ¿Dónde podría ocurrir una condición de carrera?**  
   *Respuesta:* Sí: la cola de tareas `g_taskQueue`, sincronizada con `std::mutex` y `std::condition_variable` con `std::unique_lock`.
5. **¿Qué estructuras de datos utilizó para cada diseño?**  
   *Respuesta:* `std::queue<CarUpdateTask>`, `std::condition_variable`, `std::unique_lock`, `std::vector<std::thread>`.

---

## 6. Gran Matriz Comparativa de los 4 Diseños

| Criterio de Evaluación | Diseño 1: Hilos Independientes | Diseño 2: Hilo Único | Diseño 3: Hilos por Tipo | Diseño 4: Thread Pool Asíncrono |
| :--- | :--- | :--- | :--- | :--- |
| **Arquitectura de Hilos** | 1 hilo dedicado por vehículo | 1 hilo maestro para todos | 5 hilos (1 por color) | **Pool fijo de 4 workers con cola** |
| **Aprovechamiento Multinúcleo** | Sí, dinámico por el SO | No (mononúcleo para física) | Sí (hasta 5 núcleos acotados) | **Sí, óptimo y balanceado dinámicamente** |
| **Sincronización** | Mutex global + reaper join | Mutex global básico | Cerrojos de grano fino por tipo | **Condition Variable + Queue + Mutex** |
| **Impacto con Miles de Carros** | Catastrófico (*Thread Explosion*) | Caída de FPS / Retraso | Riesgo de desbalance de carga | **Escalabilidad óptima y memoria fija** |
| **Eficiencia de Memoria** | Muy baja (stacks masivos) | Excelente (sin stacks extras) | Muy buena (solo 5 stacks) | **Óptima (solo 4 stacks fijos)** |
| **Complejidad del Código** | Moderada | Baja | Media-Alta | Alta (Patrón industrial) |
| **Mantenimiento Posterior** | Difícil de depurar a escala | Muy sencillo | Acoplado a categorías fijas | **Muy modular y altamente mantenible** |

---

## 7. Conclusiones y Reflexión del Ingeniero de Software Paralelo

> *«Un sistema concurrente no siempre necesita 'más hilos' para ser más rápido. El ingeniero de software paralelo debe seleccionar la estrategia que proporcione el mejor equilibrio entre rendimiento, complejidad del desarrollo y mantenimiento posterior del sistema.»*

El desarrollo práctico de los 4 diseños en este micro-proyecto valida categóricamente esta premisa:
1. **La falacia de 'un hilo por entidad' (Diseño 1):** Demuestra que crear hilos sin control sobrecarga la memoria y genera contención por cambios de contexto, volviéndose contraproducente a gran escala.
2. **El poder de la sencillez bien aplicada (Diseño 2):** Un solo hilo puede ser la mejor opción para simulaciones livianas, eliminando complejidades de sincronización entre entidades.
3. **El balance entre dominio y hardware (Diseño 3):** Los cerrojos de grano fino permiten paralelismo real sin contención, pero dependen de una distribución equilibrada de las categorías del mundo real.
4. **La superioridad arquitectónica del Thread Pool (Diseño 4):** Fija el uso de recursos a la capacidad física del procesador y desacopla la carga de trabajo mediante colas asíncronas, demostrando ser el estándar de ingeniería más equilibrado, escalable y profesional.
