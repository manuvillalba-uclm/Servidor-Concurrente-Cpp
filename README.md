# Servidor-Concurrente-Cpp

## Resumen
Servidor creado por Manuel Villalba Montalbán en C++ 17 utilizando un enfoque asíncrono para mantener y controlar las conexiones con los clientes. También se han utilizado hilos, sin threadpool, para realizar el cálculo y esperar (sleep) cada petición.

El servidor tiene las siguientes características:

- El servidor escuchará en un puerto que recibirá como parámetro en el arranque.
- Las peticiones que atenderá estarán formadas por cadenas en texto plano con el formato "get text n" donde text es una cadena de texto sin espacios y n es un entero positivo.
- La respuesta para un cierto text sera el hash md5 del mismo.
- La función que calcule este valor debe incluir un sleep de n milisegundos. Esto es para simular un tiempo de procesamiento variable entre peticiones. 
> Es por ello que la utilización de hilos o procesos es obligatoria, eligiéndose la primera.
- Capacidad para atender varias peticiones de manera concurrente.
> Objetivo cumplido con [select()](https://man7.org/linux/man-pages/man2/select.2.html) y los hilos.
- Capacidad para cachear los últimos C resultados. Este valor C se pasara como parámetro al arranque.
> Característica resuelta con un queue compartida entre el proceso principal y los hilos, es editada correctamente mediante mutex.
- Cuando la cache este llena y se necesite meter valores nuevos se irán eliminando aquellos que lleven mas
tiempo sin acceso.
- El servidor vaciará la cache al recibir la señal SIGUSR1
- El servidor hará un cierre ordenado al recibir la señal SIGTERM y antes de terminar mostrará los valores
cacheados por STDOUT
- Se valorará el usar utilidades de las librerías estándar que vienen en Linux en lugar de añadir
dependencias de librerías externas (boost, etc.)
- Para compilar el código se puede usar el estándar C++ 17 o inferiores (en caso de hacerlo en C++) o
C17 (en caso de C).

![Untitled Diagram drawio](https://user-images.githubusercontent.com/56063961/141977036-765a1bba-ddd3-4e67-9857-385055060b55.png)
## Compilación
```
 g++ -g *.cpp -o servidor -std=c++17 -pthread
```

## Ejecución
```
./servidor -p x -C y
```
Donde -p x es el puerto por donde escuchará el servidor, -C y es el tamaño de la caché, esta última opción no es obligatoria y por defecto es 0.

## Otros trabajos relacionados
- Proyecto del grado en ingeniería informática, es de hace dos años, y tiene también manejo de peticiones concurrente [Servidor/Cliente TCP-UDP en Python](https://github.com/manuvillalba-uclm/Servidor-Cliente-TCP-UDP-Videos)
- Proyecto personal de web-scraping, donde utilizo threadpool para acceder a diversas páginas web de manera también concurrente. (Repositorio privado)
