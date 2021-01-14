#include "sh61.hh"
#include <cstring>
#include <cerrno>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <algorithm>

volatile sig_atomic_t intr = 0;

void signal_handler(int signal)
{
    (void) signal;
    intr = 1;
}

int r = set_signal_handler(SIGINT, signal_handler);

// struct command
//    Data structure describing a command. Add your own stuff.

struct command {
    std::vector<std::string> args;
    pid_t pid; // process ID running this command, -1 if none
    int op; // operater

    command *prev; // pointer to previous command
    command *next; // pointer to next command

    int exit_status; // exit status of current process

    int read_fd; // fd command will read stdin from
    int write_fd; // fd command will redirect stdout to
    int err_fd; // fd command will redirect stderr to

    // handle redirections
    std::string filename_out;
    std::string filename_in;
    std::string filename_err;

    bool runnable;

    command();
    ~command();

    pid_t make_child(pid_t pgid);
};


// command::command()
//    This constructor function initializes a `command` structure. You may
//    add stuff to it as you grow the command structure.

command::command() {
    this->pid = -1;
    this->op = TYPE_SEQUENCE;

    this->prev = nullptr;
    this->next = nullptr;

    // initialize fds to -1 if no pipes
    this->read_fd = -1;
    this->write_fd = -1;
    this->err_fd = -1;

    this->runnable = true;
}


// command::~command()
//    This destructor function is called to delete a command.

command::~command() {

}


struct list
{
    // linked list of commands
    command *start;
    bool background;

    int pgid; // pgid of command list (pid of first command in list)

    list();
    ~list();
};

list::list()
{
    this->start = nullptr;
    this->background = false;
    this->pgid = -1;
}

list::~list()
{
    command *current = this->start;
    while (current->next)
    {
        command *tmp = current;
        current = current->next;
        delete tmp;
    }
    delete current;
}


<<<<<<< HEAD
void run_conditional(command *c)
{
    // skip next command if conditional fails
    if (c->prev->op == TYPE_AND && WEXITSTATUS(c->prev->exit_status) == EXIT_FAILURE)
    {
        c->exit_status = c->prev->exit_status;
        c->runnable = false;
=======
bool run_command(command *c, int *pgid, bool background)
{
    if (intr == 1 && !background)
    {
        c->exit_status = EXIT_FAILURE;
        intr = 0;
        return false;
    }

    if (*pgid == -1)
    {
        // shouldnt work
        *pgid = getpid();
    }

    // TODO: clean this up, put cond in sep fucntion, redirections in sep function
    // handle conditionals
    if (c->prev)
    {
        // skip next command if conditional fails
        if (c->prev->op == TYPE_AND && c->prev->exit_status != 0)
        {
            c->exit_status = c->prev->exit_status;
            return false;
        }
        else if (c->prev->op == TYPE_OR && c->prev->exit_status == 0)
        {
            c->exit_status = c->prev->exit_status;
            return false;
        }
>>>>>>> 24508ca65e08443398d83623ca74336ac1baace8
    }
    if (c->prev->op == TYPE_OR && WEXITSTATUS(c->prev->exit_status) == EXIT_SUCCESS)
    {
        c->exit_status = c->prev->exit_status;
        c->runnable = false;
    }
}

<<<<<<< HEAD
void run_redir(command *c)
{
=======
>>>>>>> 24508ca65e08443398d83623ca74336ac1baace8
    // handle redirections (TODO: put in sep function?)
    if (!c->filename_out.empty())
    {
        int f = open(c->filename_out.c_str(), O_WRONLY | O_CREAT, 0666);
        if (f == -1)
        {
            fprintf(stderr, "%s\n", strerror(errno));
<<<<<<< HEAD
            c->exit_status = -1;
            c->runnable = false;
            return;
=======
            c->exit_status = EXIT_FAILURE;
            return true;
>>>>>>> 24508ca65e08443398d83623ca74336ac1baace8
        }
        c->write_fd = f;
    }

    if (!c->filename_in.empty())
    {
        int f = open(c->filename_in.c_str(), O_RDONLY);
        if (f == -1)
        {
            fprintf(stderr, "%s\n", strerror(errno));
<<<<<<< HEAD
            c->exit_status = -1;
            c->runnable = false;
            return;
=======
            c->exit_status = EXIT_FAILURE;
            return true;
>>>>>>> 24508ca65e08443398d83623ca74336ac1baace8
        }
        c->read_fd = f;
    }

    if (!c->filename_err.empty())
    {
        int f = open(c->filename_err.c_str(), O_WRONLY | O_CREAT, 0666);
        if (f == -1)
        {
            fprintf(stderr, "%s\n", strerror(errno));
<<<<<<< HEAD
            c->exit_status = -1;
            c->runnable = false;
            return;
=======
            c->exit_status = EXIT_FAILURE;
            return true;
>>>>>>> 24508ca65e08443398d83623ca74336ac1baace8
        }
        c->err_fd = f;
    }
}

<<<<<<< HEAD
void run_pipe(command *c)
{
    // handle pipes
    int pfd[2];
    int f = pipe(pfd);
    (void) f;
    c->write_fd = pfd[1];
    c->next->read_fd = pfd[0];

    pid_t p = fork();
    if (p == 0)
    {
        c->make_child(0);
        waitpid(c->pid, &c->exit_status, 0);
        _exit(0);
    }
    else
    {
        // TODO: CLEANUP PIPE HYGEINE
        close(c->write_fd);
    }
}

// TODO: clean this up
void run_command(command *c)
{
    if (c->prev)
    {
        run_conditional(c);
    }
    
    run_redir(c);

    // handle pipes
    if (c->op == TYPE_PIPE)
    {
        // handle pipes
=======
    if (c->args.size() > 0 && c->args[0] == "cd")
    {
        int f = chdir(c->args[1].c_str());
        if (f == -1)
        {
            c->exit_status = EXIT_FAILURE;
            return true;
        }
        c->exit_status = EXIT_SUCCESS;
        return true;
    }

    // handle pipes
    if (c->op == TYPE_PIPE)
    {
>>>>>>> 24508ca65e08443398d83623ca74336ac1baace8
        int pfd[2];
        int f = pipe(pfd);
        (void) f;
        c->write_fd = pfd[1];
        c->next->read_fd = pfd[0];

        pid_t p = fork();
        if (p == 0)
        {
<<<<<<< HEAD
            c->make_child(0);
=======
            c->make_child(*pgid);
>>>>>>> 24508ca65e08443398d83623ca74336ac1baace8
            waitpid(c->pid, &c->exit_status, 0);
            _exit(0);
        }
        else
        {
            // TODO: CLEANUP PIPE HYGEINE
            close(c->write_fd);
<<<<<<< HEAD
=======
            return true;
>>>>>>> 24508ca65e08443398d83623ca74336ac1baace8
        }
    }

    // runs c and waitpid on it
    c->make_child(*pgid);
    waitpid(c->pid, &c->exit_status, 0);
}

void run_list(list *l)
{
    for (command *c = l->start; c != nullptr; c = c->next)
    {
<<<<<<< HEAD
        run_command(c);
=======
        if (!run_command(c, &l->pgid, l->background) && c->next != nullptr &&
           !(c->op == TYPE_AND || c->op == TYPE_OR))
        {
            // skip next command if conditional fails
            c = c->next;
        }
>>>>>>> 24508ca65e08443398d83623ca74336ac1baace8
    }
}


// COMMAND EXECUTION

// command::make_child(pgid)
//    Create a single child process running the command in `this`.
//    Sets `this->pid` to the pid of the child process and returns `this->pid`.
//
//    PART 1: Fork a child process and run the command using `execvp`.
//       This will require creating an array of `char*` arguments using
//       `this->args[N].c_str()`.
//    PART 5: Set up a pipeline if appropriate. This may require creating a
//       new pipe (`pipe` system call), and/or replacing the child process's
//       standard input/output with parts of the pipe (`dup2` and `close`).
//       Draw pictures!
//    PART 7: Handle redirections.
//    PART 8: The child process should be in the process group `pgid`, or
//       its own process group (if `pgid == 0`). To avoid race conditions,
//       this will require TWO calls to `setpgid`.

pid_t command::make_child(pid_t pgid) {
    // (void) pgid; // You won’t need `pgid` until part 8.

    pid_t p = fork();
    if (p == 0)
    {
        setpgid(0, pgid);

        if (this->read_fd != -1)
        {
            dup2(this->read_fd, 0);
            close(this->read_fd);
        }
        if (this->write_fd != -1)
        {
            dup2(this->write_fd, 1);
            close(this->write_fd);
        }
        if (this->err_fd != -1)
        {
            dup2(this->err_fd, 2);
            close(this->err_fd);
        }

        // copy args vector to args array since execvp takes null termniated array
        std::vector<const char *> argv;
        for (auto it = this->args.begin(); it !=  this->args.end(); ++it)
        {
            argv.push_back(it->c_str());
        }
        argv.push_back(NULL);

        if (argv[0] == NULL)
        {
            _exit(EXIT_FAILURE);
        }

        int f = execvp(argv[0], (char *const *) argv.data());
        if (f == -1)
        {
            _exit(EXIT_FAILURE);
        }
    }
    else if (p > 0)
    {
        setpgid(p, pgid);
        this->pid = p;
        return p;
    }

    // fork failed
    return -1;
}


// run(c)
//    Run the command *list* starting at `c`. Initially this just calls
//    `make_child` and `waitpid`; you’ll extend it to handle command lists,
//    conditionals, and pipelines.
//
//    PART 1: Start the single command `c` with `c->make_child(0)`,
//        and wait for it to finish using `waitpid`.
//    The remaining parts may require that you change `struct command`
//    (e.g., to track whether a command is in the background)
//    and write code in `run` (or in helper functions).
//    PART 2: Treat background commands differently.
//    PART 3: Introduce a loop to run all commands in the list.
//    PART 4: Change the loop to handle conditionals.
//    PART 5: Change the loop to handle pipelines. Start all processes in
//       the pipeline in parallel. The status of a pipeline is the status of
//       its LAST command.
//    PART 8: - Choose a process group for each pipeline and pass it to
//         `make_child`.
//       - Call `claim_foreground(pgid)` before waiting for the pipeline.
//       - Call `claim_foreground(0)` once the pipeline is complete.

void run(std::vector<list *> line) {
    for (auto &list : line)
    {
        // start background process in background shell
        if (list->background)
        {
            pid_t p = fork();
            if (p == 0)
            {
                run_list(list);
                _exit(0);
            }
        }
        else
        {
            claim_foreground(list->pgid);
            run_list(list);
            claim_foreground(0);
        }
    }
}


// parse_line(s)
//    Parse the command list in `s` and return it. Returns `nullptr` if
//    `s` is empty (only spaces). You’ll extend it to handle more token
//    types.

// TODO: MAKE THIS CLEAN

std::vector<list *> parse_line(const char* s) {
    std::vector<list *> line; // line of lists
    list *current_list = new list; // current list of commands
    command *current_command = new command; // current commmand

    current_list->start = current_command;

    shell_parser parser(s);
    for (shell_token_iterator it = parser.begin(); it != parser.end(); ++it) {

        if (it.type() == TYPE_NORMAL)
        {
            current_command->args.push_back(it.str());
        }
        else if (it.type() == TYPE_SEQUENCE || it.type() == TYPE_BACKGROUND)
        {
            if (it.type() == TYPE_BACKGROUND)
            {
                current_list->background = true;
            }
            line.push_back(current_list);
            current_list = new list;
            current_command = new command;
            current_list->start = current_command;
        }
        else
        {
            if (it.type() == TYPE_REDIRECT_OP)
            {
                char redir_type = it.str()[0];
                ++it;
                if (redir_type == '>')
                {
                    current_command->filename_out = it.str();
                }
                else if (redir_type == '<')
                {
                    current_command->filename_in = it.str();
                }
                else
                {
                    current_command->filename_err = it.str();
                }
                continue;
            }
            current_command->op = it.type();
            current_command->next = new command;
            current_command->next->prev = current_command;
            current_command = current_command->next;
        }
    }
    line.push_back(current_list);

    return line;
}


int main(int argc, char* argv[]) {
    std::vector<std::vector<list *>> lines;

    FILE* command_file = stdin;
    bool quiet = false;

    // Check for '-q' option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0) {
        quiet = true;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1) {
        command_file = fopen(argv[1], "rb");
        if (!command_file) {
            perror(argv[1]);
            exit(1);
        }
    }

    // - Put the shell into the foreground
    // - Ignore the SIGTTOU signal, which is sent when the shell is put back
    //   into the foreground
    claim_foreground(0);
    set_signal_handler(SIGTTOU, SIG_IGN);

    char buf[BUFSIZ];
    int bufpos = 0;
    bool needprompt = true;

    while (!feof(command_file)) {
        // Print the prompt at the beginning of the line
        if (needprompt && !quiet) {
            printf("sh61[%d]$ ", getpid());
            fflush(stdout);
            needprompt = false;
        }

        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == nullptr) {
            if (ferror(command_file) && errno == EINTR) {
                // ignore EINTR errors
                clearerr(command_file);
                buf[bufpos] = 0;
            } else {
                if (ferror(command_file)) {
                    perror("sh61");
                }
                break;
            }
        }

        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
            std::vector<list *> line = parse_line(buf);
            lines.push_back(line); // to be eventually reaped
            if (line.size() == 0)
            {
                continue;
            }
            run(line);
            bufpos = 0;
            needprompt = 1;
        }

        // Handle zombie processes and/or interrupt requests
        for (auto &line : lines)
        {
            for (auto &list : line)
            {
                for (command *c = list->start; c != nullptr; c = c->next)
                {
                    waitpid(c->pid, &c->exit_status, WNOHANG);
                }
            }
        }
    }
    // TODO: handle deletion here (delete list)
    for (auto &line : lines)
    {
        for (auto &list : line)
        {
            delete list;
        }
    }

    return 0;
}
