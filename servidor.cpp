/**
    Servidor concurrente para prueba Meteologica
    @file servidor.cpp
    @author Manuel Villalba Montalbán
    @version 1.0 15/11/2021
*/

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <algorithm>
#include <sstream>
#include <thread>
#include <queue>
#include <csignal>
#include <mutex>
#include <vector>
#include "md5.h"

bool active = true;
int main_server_fd, n_cache, active_threads = 0, max_clients = 50;
int client_sockets_fd[50] = {0};

/*Mutex para controlar las variables compartidas client_sockets_fd, cache y 
active_threads
*/

std::mutex socket_mutex, cache_mutex, act_th_mutex;
std::queue<std::pair<std::string, std::string>> cache;
MD5 md5;

/*
Clase utilizada para atender los argumentos en línea de comandos, tiene dos
funciones, get_cmd_option para controlar un argumento seguido de una opción,
como -p 3456, y cmd_option_exists para una opción sin argumento como un -h.

Fuente: https://stackoverflow.com/a/868894/16834425

*/
class InputParser
{
public:
    InputParser(int &argc, char **argv)
    {
        for (int i = 1; i < argc; ++i)
            this->tokens.push_back(std::string(argv[i]));
    }
    const std::string &get_cmd_option(const std::string &option) const
    {
        std::vector<std::string>::const_iterator itr;
        itr = std::find(this->tokens.begin(), this->tokens.end(), option);
        if (itr != this->tokens.end() && ++itr != this->tokens.end())
        {
            return *itr;
        }
        static const std::string empty_string("");
        return empty_string;
    }
    bool cmd_option_exists(const std::string &option) const
    {
        return std::find(this->tokens.begin(), this->tokens.end(), option) != this->tokens.end();
    }

private:
    std::vector<std::string> tokens;
};

//Utilizada para separar la request get text n en 3 strings.
void tokenize(std::string const &str, const char delim,
              std::vector<std::string> &out)
{
    std::stringstream ss(str);

    std::string s;
    while (std::getline(ss, s, delim))
    {
        out.push_back(s);
    }
}

//Imprime por salida estandar una pareja.
void print_pair(std::pair<std::string, std::string> p)
{
    std::cout << "("
              << p.first << ", "
              << p.second << ")\n";
}

//Imprime por salida estandar la cola de caché.
void showstack(std::queue<std::pair<std::string, std::string>> s)
{
    while (!s.empty())
    {
        print_pair(s.front());
        s.pop();
    }

    std::cout << '\n';
}

//Divide la request y comprueba si está bien formada, sino lanza una excepción.
std::vector<std::string> verify_request(std::string request)
{
    const char delim = ' ';
    std::vector<std::string> request_fragment;
    tokenize(request, delim, request_fragment);

    if (request_fragment.size() != 3)
    {
        throw std::invalid_argument("request must contain just three text strings: get text n");
    }
    if (request_fragment[0] != "get")
    {
        throw std::invalid_argument("unknown request");
    }
    if ((request_fragment[2] != "0" && atoi(request_fragment[2].c_str()) == 0) || (atoi(request_fragment[2].c_str()) < 0))
    {
        printf("%i/n", atoi(request_fragment[2].c_str()));
        throw std::invalid_argument("invalid n");
    }
    return request_fragment;
}

//Introduce nuevas parejas a la cola caché, y aplica la regla de que si supera -C n va eliminando la más antigua (FIFO).
void insert_data_to_cache(std::string request, std::string md5_request)
{
    if (n_cache > 0)
    {
        if (cache.size() < n_cache)
        {
            cache.push({request, md5_request});
        }
        else
        {
            cache.pop();
            cache.push({request, md5_request});
        }
    }
}
/*
    Función utilizada por los hilos "separados" (detach), utilizo mutex
    para modificar variables compartidas. Hace el cáculo de md5 y espera
    el tiempo correspondiente, cuando acaba envía y hace shutdown 
    dejandolo libre para su reuso.
*/
void wait_send(std::string request, int i)
{
    /*
    Tenemos que saber cuántos hilos tenemos activos por si llega una señal
    SIGTERM
    */
    act_th_mutex.lock();
    active_threads += 1;
    act_th_mutex.unlock();

    int client = client_sockets_fd[i];

    std::vector<std::string> request_fragment;
    std::string request_md5, new_line = "\n";

    try
    {
        request_fragment = verify_request(request);
        //Calculamos md5
        request_md5 = md5(request_fragment[1]);
        std::string mensaje_str = request_md5 + new_line;
        int sleep_time = atoi(request_fragment[2].c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));

        const char *mensaje = mensaje_str.c_str();

        send(client, mensaje, strlen(mensaje), 0);
        shutdown(client, SHUT_RDWR);

        socket_mutex.lock();
        //liberamos para su reuso
        client_sockets_fd[i] = 0;
        socket_mutex.unlock();

        cache_mutex.lock();
        insert_data_to_cache(request_fragment[1], request_md5);
        cache_mutex.unlock();
    }
    catch (const std::invalid_argument &e)
    {
        std::cout << "Caught exception \"" << e.what() << "\"\n";
    }
    act_th_mutex.lock();
    active_threads -= 1;
    act_th_mutex.unlock();
}
//El swap es de la maneras más eficientes de limpiar la cola
void sigusr1_handler(int signal)
{
    std::queue<std::pair<std::string, std::string>> empty;
    cache_mutex.lock();
    std::swap(cache, empty);
    cache_mutex.unlock();
    std::cout << "Done!\n";
}

/*
Con active = false no se crean más hilos, y finaliza el bucle
que contiene select(), espera a que todos los hilos en
funcionamiento acaben y cierra el descriptor de socket principal, 
lo que produce -1 en accept().
*/
void sigterm_handler(int signal)
{
    active = false;
    while (active_threads > 0)
    {
        sleep(1);
    }
    close(main_server_fd);
}


int main(int argc, char *argv[])
{
    signal(SIGUSR1, sigusr1_handler);
    signal(SIGTERM, sigterm_handler);

    InputParser input(argc, argv);
    std::string port_s, n_cache_s;
    int port;

    int request_len = 4084, buffer_len = 1024;
    char request[request_len] = {0}, buffer[buffer_len] = {0};
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    int i, max_socket_descriptor, socket_descriptor, new_socket, activity, valread;

    fd_set fd_read_sockets;

    //Argumentos por línea de comandos
    port_s = input.get_cmd_option("-p");
    port = atoi(port_s.c_str());
    if ((port_s != "0" && port == 0) || (port < 0 || port > 65535))
    {
        fprintf(stderr, "ERROR, no valid port value\n");
        exit(1);
    }
    n_cache_s = input.get_cmd_option("-C");
    if (n_cache_s == "")
    {
        n_cache_s = "0";
    }
    n_cache = atoi(n_cache_s.c_str());
    if ((n_cache_s != "0" && n_cache == 0) || (n_cache < 0))
    {
        fprintf(stderr, "ERROR, no valid cache value\n");
        exit(1);
    }

    //Inicializamos el server
    if ((main_server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        fprintf(stderr, "ERROR, creation of socket file descriptor failed\n");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(main_server_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
    {
        fprintf(stderr, "ERROR, set socket options failed\n");
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(main_server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        fprintf(stderr, "ERROR, bind failed\n");
        exit(1);
    }

    printf("Server on port: %d, pid: %d \n", port, getpid());
    if (listen(main_server_fd, 3) < 0)
    {
        fprintf(stderr, "ERROR, listen failed\n");
        exit(1);
    }

    /*
    client_sockets_fd es un array de sockets que comienza todos sus valores a 0 y va 
    reemplazándolos con el descriptor de socket del cliente conforme van llegando nuevas
    peticiones. Cuando el cliente se desconecta durante la espera o el servidor contesta
    al cliente vuelven a 0 para ser reutilizadas.
    */
    while (active)
    {
        FD_ZERO(&fd_read_sockets);
        FD_SET(main_server_fd, &fd_read_sockets);
        max_socket_descriptor = main_server_fd;

        for (i = 0; i < max_clients; i++)
        {
            socket_descriptor = client_sockets_fd[i];
            if (socket_descriptor > 0)
            {
                FD_SET(socket_descriptor, &fd_read_sockets);
            }

            if (socket_descriptor > max_socket_descriptor)
            {
                max_socket_descriptor = socket_descriptor;
            }
        }
        /*
        Utilizamos select() cuando queremos monitorizar varios file descriptors, esperando
        a que alguno de ellos esté listo para una operación de I/O
        */

        activity = select(max_socket_descriptor + 1, &fd_read_sockets, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR))
        {
            fprintf(stderr, "ERROR, socket select failed\n");
        }
        /*
        Si llega algo al socket principal (main_main_server_fd) es que hay una nueva conexión
        o se cierra el socket principal, si es el último caso devuelve -1.
        */
         
        if (FD_ISSET(main_server_fd, &fd_read_sockets))
        {
            if ((new_socket = accept(main_server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
            {
                //Se ha cerrado el socket mediante SIGTERM
                if (new_socket == -1)
                {
                    printf("Clossing the server\n");
                    showstack(cache);
                    break;
                }
                fprintf(stderr, "ERROR, new socket accept failed\n");
            }

            
            printf("New connection , socket fd is %d , ip is : %s , port : %d\n", new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

            //añadimos nuevo socket al array de sockets
            for (i = 0; i < max_clients; i++)
            {
                //if position is empty
                if (client_sockets_fd[i] == 0)
                {
                    socket_mutex.lock();
                    client_sockets_fd[i] = new_socket;
                    printf("Adding to list of sockets as %d\n", i);
                    socket_mutex.unlock();
                    break;
                }
            }
        }
        for (i = 0; i < max_clients; i++)
        {
            socket_descriptor = client_sockets_fd[i];

            if (FD_ISSET(socket_descriptor, &fd_read_sockets))
            {
                //Vemos si es una desconexión.
                if ((valread = read(socket_descriptor, request, request_len)) == 0)
                {
                    //Un cliente se ha desconectado.
                    getpeername(socket_descriptor, (struct sockaddr *)&address,
                                (socklen_t *)&addrlen);
                    printf("Host disconnected , ip %s , port %d \n",
                           inet_ntoa(address.sin_addr), ntohs(address.sin_port));

                    //Cerramos el socket y lo marcamos como 0 para su reuso.
                    close(socket_descriptor);
                    socket_mutex.lock();
                    client_sockets_fd[i] = 0;
                    socket_mutex.unlock();
                }

                //Devolvemos la petición en md5.
                else
                {

                    std::string request_s(request);
                    //active es que no se ha enviado un SIGTERM
                    if (active)
                    {   
                        //Inicializamos un hilo
                        std::thread t1(wait_send, request_s, i);
                        /*Obviamente usamos detach en vez de join porque sino
                        el hilo principal tendría que esperar también el sleep.
                        */

                        t1.detach();
                    }
                }
            }
        }
    }
    return 0;
}