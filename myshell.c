#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "parser.h"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_BG_COMMANDS 30

typedef struct job
{
	int ncom;
	pid_t *pid;		// array de pids
	char *linea_bg; // string con procesos
	char **status;
	bool disponible;
	bool done; // true si ha terminado

} Tjobs;

// funciones
void input(tline *line);

void output(tline *line);

void outputerror(tline *line);

void pipes_ges_hijo(int ncom, int i, int **pipes);

void cd(tline *line);

void freeJob(Tjobs *job); // tiene que recibir la posición del job

void manejador();

void manejador2();

void exitFun(void);

void setUmask(char *string);

bool isOctal(char *s)
{
	while (*s)
	{
		if (*s < '0' || *s > '7')
			return false;
		s++;
	}
	return true;
};

void comprobarEstadoHijos();

bool comprobarUnHijo(int p_job, int hijo);

// variables globales
Tjobs *jobs;
int pos_fg; // la posicion del comando que queremos pasar a foreground

int main(void)
{
	char buf[1024];
	tline *line;
	int i, j, ncom, aux;
	int **pipes;
	int counter = 0;

	// background

	jobs = (Tjobs *)malloc(MAX_BG_COMMANDS * sizeof(Tjobs));
	for (i = 0; i < MAX_BG_COMMANDS; i++)
	{
		jobs[i].disponible = true; // todas la posiciones empiezan estando disponibles
		jobs[i].ncom = 0;
		jobs[i].done = false;
	}

	signal(SIGINT, manejador);

	printf("msh> ");

	while (fgets(buf, 1024, stdin))
	{
		pos_fg = counter;
		signal(SIGINT, manejador2);

		comprobarEstadoHijos();

		line = tokenize(buf);
		if (line == NULL)
		{
			continue;
		}

		ncom = line->ncommands;

		jobs[counter].pid = (pid_t *)malloc(ncom * sizeof(pid_t));

		if (ncom > 1)
		{
			pipes = (int **)malloc((ncom - 1) * sizeof(int *));
			for (i = 0; i < ncom - 1; i++)
			{
				pipes[i] = (int *)malloc(2 * sizeof(int));
				if (pipe(pipes[i]) < 0)
				{ // comprobación de error
					printf("Error con las pipes\n");
					exit(5);
				}
			}
		}

		if (ncom != 0 &&
			strcmp(line->commands[0].argv[0], "cd") != 0 &&
			strcmp(line->commands[0].argv[0], "jobs") != 0 &&
			strcmp(line->commands[0].argv[0], "fg") != 0 &&
			strcmp(line->commands[0].argv[0], "exit") != 0 &&
			strcmp(line->commands[0].argv[0], "umask") != 0)
		{ // devuelve 0 cuando son iguales
			for (i = 0; i < ncom; i++)
			{
				jobs[counter].pid[i] = fork();

				if (jobs[counter].pid[i] < 0)
				{
					fprintf(stderr, "fallo en el fork()");
					exit(1);
				}
				else if (jobs[counter].pid[i] == 0) // esto solo lo hacen los hijos
				{
					if (line->background) // si es un hijo de un proceso de background
					{					  // se ignora Ctrl + C
						signal(SIGINT, SIG_IGN);
					}

					// aquí se redirecciona la entrada del primer proceso en caso de ser necesario
					if (i == 0 && line->redirect_input != NULL)
					{
						input(line);
					}
					if (i == ncom - 1 && line->redirect_output != NULL)
					{
						output(line);
					}
					if (i == ncom - 1 && line->redirect_error != NULL)
					{
						outputerror(line);
					}

					if (ncom > 1)
					{
						pipes_ges_hijo(ncom, i, pipes);
					}

					execvp(line->commands[i].argv[0], line->commands[i].argv);
					// si se ejecuta esto es que ha habido un error
					printf("%s: No se encuentra el mandato\n", line->commands[i].argv[0]);
					exit(1);
				}
			}

			for (j = 0; j < ncom - 1; j++) // cerramos todas las pipes
			{
				close(pipes[j][0]);
				close(pipes[j][1]);
				free(pipes[j]);
			}
			if (!line->background)
			{
				for (j = 0; j < ncom; j++)
				{
					waitpid(jobs[counter].pid[j], NULL, 0);
				}
				free(jobs[counter].pid);
				jobs[counter].ncom = 0;
			}
			else // si es un proceso del background
			{
				jobs[counter].ncom = ncom;
				jobs[counter].linea_bg = strdup(buf);
				jobs[counter].status = (char **)malloc(ncom * sizeof(char *));
				jobs[counter].disponible = false;

				// Inicializar el estado de cada proceso en segundo plano
				for (i = 0; i < ncom; i++)
				{
					jobs[counter].status[i] = "Running";
				}
				printf("[%d] %d\n", counter + 1, jobs[counter].pid[ncom - 1]);
				// fflush(stdout);
				if (counter == MAX_BG_COMMANDS - 1)
				{
					counter = 0;
				}
				while (!jobs[counter].disponible)
				{
					counter++;
				}
			}
		}
		else if (ncom == 1 && strcmp(line->commands[0].argv[0], "cd") == 0)
		{ // esto es el comando cd
			cd(line);
		}
		else if (ncom == 1 && strcmp(line->commands[0].argv[0], "jobs") == 0)
		{
			for (i = 0; i < MAX_BG_COMMANDS; i++)
			{
				if (!jobs[i].disponible) // si la posicion no es ta disponible, eso significa que ya hay un proceso en esa posición
				{
					if (jobs[i].done)
					{ // si ha terminado el proceso no se muestra el caracter '&'
						aux = strlen(jobs[i].linea_bg);
						jobs[i].linea_bg[aux - 2] = '\n';
						jobs[i].linea_bg[aux - 1] = '\0';
					}
					printf("[%d]\t%s\t\t%s", i + 1, jobs[i].status[jobs[i].ncom - 1], jobs[i].linea_bg);
				}
				if (jobs[i].done)
				{
					freeJob(&jobs[i]); // ha que pasar la posicioón en memoria de jobs[i]
					// se comprueba si ese espacio de jobs esta disponible
					// luego se comprueba si el espacio counter - 1 esta disponible tambien
					// en cuyo caso se cambia counter a counter - 1
					while (jobs[counter].disponible && counter - 1 >= 0 && jobs[counter - 1].disponible)
					{
						counter -= 1;
					}
				}
			}
		}
		else if (ncom == 1 && strcmp(line->commands[0].argv[0], "fg") == 0)
		{
			if (counter != 0 && line->commands->argc < 3)
			{
				if (line->commands->argc == 2)
				{
					pos_fg = atoi(line->commands->argv[1]) - 1;
					// en el if siguiente se comprueba que no le pasen cifras que se salgan 
					// del rango 0 - MAX_BG_COMMANDS
				}
				else
				{ // si no nos pasan ningun argumento, se coje el último job

					counter -= 1;
					while (counter >= 0 && !jobs[counter].disponible && jobs[counter].done) // si ha terminado el job liberamos el espacio y pasamos al siguiente
					{
						// si el primer job ya está terminado se muestra por pantalla y se pasa el siguiente que no este completo al foreground
						printf("[%d]\t%s\t\t%s", counter + 1, jobs[counter].status[jobs[counter].ncom - 1], jobs[counter].linea_bg);
						freeJob(&jobs[counter]);

						counter--;
					}
					pos_fg = counter;
				}
				if (pos_fg >= 0 && pos_fg <= MAX_BG_COMMANDS && !jobs[pos_fg].disponible) // hay que comprobar si esa posicion de jobs tiene algo guardado
				{
					for (i = 0; i < jobs[pos_fg].ncom; i++)
					{
						// comprobamos si los procesos estan terminados
						if (strcmp(jobs[pos_fg].status[i], "Running") == 0)
						{
							waitpid(jobs[pos_fg].pid[i], NULL, 0);
						}
					}
					freeJob(&jobs[pos_fg]); // ha que pasar la posicioón en memoria de jobs[i]
				}
				else if (pos_fg < 0)
				{
					counter = 0; // porque si pos_fg es negativo 
								 // eso significa que en el while anterior counter se ha vuelto negativo
				}
				else
				{
					printf("fg: %d: no such job\n", pos_fg);
				}
			}
			else
			{ // si counter == 0
				fprintf(stderr, "No hay procesos en el background\n");
			}
		}
		else if (ncom == 1 && strcmp(line->commands[0].argv[0], "umask") == 0)
		{
			setUmask(line->commands[0].argv[1]);
		}
		else if (ncom == 1 && strcmp(line->commands[0].argv[0], "exit") == 0)
		{
			exitFun();
			break;
		}

		else if (ncom != 0) // porque si es igual a 0 no se hace nada
		{
			fprintf(stderr, "Has usado comandos internos con pipes\n");
		}
		if (ncom > 1)
			free(pipes);

		printf("msh> ");
		signal(SIGINT, manejador);
	}

	free(jobs);

	return 0;
}

void manejador()
{
	printf("\nmsh> ");
	fflush(stdout);
}

void manejador2()
{

	int i;
	for (i = 0; i < jobs[pos_fg].ncom; i++)
	{
		// comprobamos si los procesos estan terminados
		if (strcmp(jobs[pos_fg].status[i], "Running") == 0)
		{
			kill(jobs[pos_fg].pid[i], SIGKILL);
		}
	}
	// freeJob(&jobs[pos_fg]); // ha que pasar la posicioón en memoria de jobs[i]
	printf("\n");
}

void input(tline *line)
{
	int fdin;
	fdin = open(line->redirect_input, O_RDONLY);
	if (fdin < 0)
	{
		printf("%s: Error. No se ha podido abrir el fichero\n",line->redirect_input );
		exit(1);
	}
	dup2(fdin, 0);
	close(fdin);
}

void output(tline *line)
{
	int fdout;
	fdout = creat(line->redirect_output, 0666 & ~umask(0)); // Es 0666 en ficheros y 0777 en carpetas
	if (fdout < 0)
	{
		printf("%s: Error. No se ha podido acceder/crear el fichero\n", line->redirect_output);
		exit(1);
	}
	dup2(fdout, 1);
	close(fdout);
}

void outputerror(tline *line)
{
	int fderr;
	fderr = creat(line->redirect_error, 0666 & ~umask(0));
	if (fderr < 0)
	{
		printf("%s: Error. No se ha podido acceder/crear el fichero\n", line->redirect_output);
		exit(1);
	}
	dup2(fderr, 2);
	close(fderr);
}

void pipes_ges_hijo(int ncom, int i, int **pipes)
{
	if (i == 0)
	{ // solo puede escribir en el pipe de i+1
		dup2(pipes[i][1], 1);
	}
	else if (i == ncom - 1)
	{
		dup2(pipes[i - 1][0], 0);
	}
	else // puede lee de si mismo y escribir en el siguiente
	{
		dup2(pipes[i - 1][0], 0);
		dup2(pipes[i][1], 1);
	}

	int j;
	for (j = 0; j < ncom - 1; j++)
	{
		close(pipes[j][0]);
		close(pipes[j][1]);
	}
}

void cd(tline *line)
{
	char *dir;
	char directorioActual[512];
	if (line->commands[0].argc == 1)
	{
		dir = getenv("HOME");
		if (dir == NULL)
		{
			fprintf(stderr, "No existe la variable $HOME\n");
		}
	}
	else
	{
		dir = line->commands[0].argv[1];
	}

	// Comprobar si es un directorio
	if (chdir(dir) != 0)
	{
		fprintf(stderr, "Error al cambiar de directorio: %s\n", strerror(errno));
	}
	printf("El directorio actual es: %s\n", getcwd(directorioActual, -1));
}

void freeJob(Tjobs *job) // tiene que recibir la posición del job
{

	free(job->pid);
	free(job->linea_bg);
	free(job->status);

	job->disponible = true;
	job->ncom = 0;
	job->done = false;
}

void exitFun(void)
{
	printf("exit\n");
	fflush(stdout);
	int i, j;
	for (i = 0; i < MAX_BG_COMMANDS; i++)
	{
		if (jobs[i].ncom != 0) // si ncom es distinto de 0 es que hay un proceso en esa posición
		{
			for (j = 0; j < jobs[i].ncom; j++)
			{
				if (strcmp(jobs[i].status[j], "Running") == 0)
				{
					kill(jobs[i].pid[j], SIGKILL);
				}
			}
			freeJob(&jobs[i]);
		}
	}
}

void setUmask(char *string)
{
	mode_t mask, oldmask;

	if (string == NULL)
	{
		oldmask = umask(0);
		umask(oldmask);
		printf("%03o\n", oldmask);
		return;
	}
	else
	{
		if (strlen(string) > 4)
		{
			printf("Número octal fuera del rango");
			return;
		}
		if (!isOctal(string))
		{
			printf("El argumento debe ser un octal");
			return;
		}
		errno = 0; // en teoría es una variable global de C en la que
				   // strtol dejaría su error en caso de que hubiera
		mask = strtol(string, NULL, 8);
		if (errno)
		{
			printf("Error al convertir el argumento a octal\n");
			return;
		}

		oldmask = umask(mask);
		if (strlen(string) == 4)
		{					 // para que en el caso de que se pasen 4 cifras por entrada
			mask = umask(0); // solo se muestren por pantalla las 3 finales
			umask(mask);
		}
		printf("%03o\n", mask);
	}
}

void comprobarEstadoHijos()
{
	int i, j;
	for (i = 0; i < MAX_BG_COMMANDS; i++)
	{
		if (!jobs[i].disponible)
		{
			bool done = true;
			j = 0;
			while (j < jobs[i].ncom && done)
			{
				// si jobs[i].status[j] == "Running"
				if (strcmp(jobs[i].status[j], "Running") == 0)
				{ // comprobamos si los procesos estan terminados
					done = done && comprobarUnHijo(i,j);
				}
				j++;
			}
			if (done)
			{
				jobs[i].done = done;
			}
		}
	}
}

bool comprobarUnHijo(int p_job, int hijo)
{
	int wait_status;
	pid_t child_pid = waitpid(jobs[p_job].pid[hijo], &wait_status, WNOHANG);
	if (child_pid > 0)
	{
		// El proceso ha terminado, actualiza el estado
		if (WIFEXITED(wait_status))
		{
			jobs[p_job].status[hijo] = strdup("Done");
		}
		else if (WIFSIGNALED(wait_status))
		{
			jobs[p_job].status[hijo] = strdup("Exit");
		}
		return true;
	}
	else if (child_pid != 0)
	{
		// Manejar error en waitpid
		perror("waitpid");
	}
	return false;
}
