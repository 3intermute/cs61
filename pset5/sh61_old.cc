#include "sh61.hh"
#include <cstring>
#include <cerrno>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <algorithm>



// struct command
//    Data structure describing a command. Add your own stuff.

struct command {
    std::vector<std::string> args;
    pid_t pid;      // process ID running this command, -1 if none
    int op; // operater

    command *prev; // pointer to previous command
    command *next; // pointer to next command

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
}


// command::~command()
//    This destructor function is called to delete a command.

command::~command() {
    // traverse command list and free
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
    if (this->args.size() <= 0)
    {
        return -1;
    }
    (void) pgid; // You won’t need `pgid` until part 8.

    pid_t p = fork();

    if (p == 0)
    {
        // child executes command

        // copy args vector to args array
        // referenced: https://stackoverflow.com/questions/7048888/stdvectorstdstring-to-char-array
        std::vector<const char *> argv;
        std::transform(this->args.begin(), this->args.end(), std::back_inserter(argv),
            [](std::string &s){ return s.c_str(); }
        );
        // argv array must be null terminated
        argv.push_back(NULL);

        int f = execvp(argv[0], (char *const *) argv.data());
        if (f == -1)
        {
            _exit(EXIT_FAILURE);
        }
    }
    else if (p > 0)
    {
        // returns child pid to parent
        this->pid = p;
        return p;
    }

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

void run(command* c) {
    // call make_child for each node in command list
    // bool back = false;
    for (command *current = c; current != nullptr; current = current->next)
    {
        // if (back && (current->op == TYPE_SEQUENCE || current->op == TYPE_BACKGROUND))
        // {
        //     _exit(0);
        // }

        command *prev = current->prev;
        if (prev)
        {
            // handle cocnditionals
            if (prev->op == TYPE_AND && WEXITSTATUS(prev->exit_stat) == EXIT_FAILURE)
            {
                current->exit_stat = prev->exit_stat;
                continue;
            }
            else if (prev->op == TYPE_OR && WEXITSTATUS(prev->exit_stat) == EXIT_SUCCESS)
            {
                current->exit_stat = prev->exit_stat;
                continue;
            }
        }

        if (current->background && (current->op == TYPE_AND || current->op == TYPE_OR))
        {
            pid_t p = fork();
            if (p == 0)
            {
                command *cb = current;
                while (cb->background)
                {
                    // command *prev_cb = cb->prev;
                    // if (prev)
                    // {
                    //     // handle cocnditionals
                    //     if (prev_cb->op == TYPE_AND && WEXITSTATUS(prev_cb->exit_stat) == EXIT_FAILURE)
                    //     {
                    //         cb->exit_stat = prev_cb->exit_stat;
                    //         continue;
                    //     }
                    //     else if (prev->op == TYPE_OR && WEXITSTATUS(prev->exit_stat) == EXIT_SUCCESS)
                    //     {
                    //         cb->exit_stat = prev->exit_stat;
                    //         continue;
                    //     }
                    // }
                    cb->make_child(0);
                    waitpid(c->pid, &c->exit_stat, 0);
                    cb = cb->next;
                }
                _exit(0);
                // back = true;
            }
            else
            {
                current = current->next;
            }
        }
        else
        {
            int waitpid_opts = 0;
            if (current->background)
            {
                waitpid_opts = WNOHANG;
            }
            current->make_child(0);
            pid_t exited_pid = waitpid(current->pid, &current->exit_stat, waitpid_opts);
            (void) exited_pid;
        }

        // current->make_child(0);
        //
        // // shell waits for child to exit
        // int waitpid_opts = 0;
        // if (current->background)
        // {
        //     // handle background
        //     waitpid_opts = WNOHANG;
        // }
        //
        // int status;
        // // keep exited pid to reap zombies on nohang
        // pid_t exited_pid = waitpid(current->pid, &status, waitpid_opts);
        // (void) exited_pid;
        //
        // // set exit status
        // current->exit_stat = status;
    }
}


// parse_line(s)
//    Parse the command list in `s` and return it. Returns `nullptr` if
//    `s` is empty (only spaces). You’ll extend it to handle more token
//    types.

command* parse_line(const char* s) {
    shell_parser parser(s);

    command *start = new command;
    command *current = start;
    // allocate_new is a ghetto check if iterator reached (end - 1)
    bool allocate_new = false;

    for (shell_token_iterator it = parser.begin(); it != parser.end(); ++it) {
        if (allocate_new)
        {
            current->next = new command;
            current->next->prev = current;
            current = current->next;
            allocate_new = false;
        }

        if (it.type() == TYPE_NORMAL)
        {
            current->args.push_back(it.str());
        }
        else
        {
            current->op = it.type();
            if (it.type() == TYPE_BACKGROUND)
            {
                current->background = true;
            }
            allocate_new = true;
        }
    }

    // set
    bool is_background = false;
    for (command *c = current; c != nullptr; c = c->prev)
    {
        if (c->background)
        {
            is_background = true;
        }
        else if (c->op == TYPE_SEQUENCE)
        {
            is_background = false;
        }

        if (is_background)
        {
            c->background = true;
        }
    }

    return start;
}


int main(int argc, char* argv[]) {
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
            if (command* c = parse_line(buf)) {
                run(c);
                delete c;
            }
            bufpos = 0;
            needprompt = 1;
        }

        // Handle zombie processes and/or interrupt requests
        // Your code here!
    }

    return 0;
}
