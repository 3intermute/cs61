#include "io61.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <climits>
#include <cerrno>
#include <algorithm>

#define CACHESIZE 16392

// io61.c
//    YOUR CODE HERE!


// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

struct io61_file {
    int fd; // file descriptor used for systemcalls
    int mode;
    char cache[CACHESIZE]; // cache for file
    off_t tag;      // file offset of first byte in cache (0 when file is opened)
    off_t end_tag;  // file offset one past last valid byte in cache
    off_t pos_tag;  // file offset of next char to read in cache
};

int io61_fill(io61_file* f);

// io61_fdopen(fd, mode)
//    Return a new io61_file for file descriptor `fd`. `mode` is
//    either O_RDONLY for a read-only file or O_WRONLY for a
//    write-only file. You need not support read/write files.
// return nullptr on failure

io61_file* io61_fdopen(int fd, int mode) {
    assert(fd >= 0);
    io61_file* f = new io61_file;
    f->fd = fd;
    f->mode = mode;
    f->end_tag = 0;

    return f;
}


// io61_close(f)
//    Close the io61_file `f` and release all its resources.

int io61_close(io61_file* f) {
    io61_flush(f);
    int r = close(f->fd);
    delete f;
    return r;
}

int io61_fill(io61_file* f) {
    // advance tag, pos_tag to previous end_tag
    f->tag = f->pos_tag = f->end_tag;

    ssize_t nread = read(f->fd, f->cache, CACHESIZE);
    if (nread >= 0)
    {
        // update end_tag
        f->end_tag = f->tag + nread;
        return 0;
    }
    return -1;
}

// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file* f) {
    if (f->pos_tag == f->end_tag)
    {
        if (io61_fill(f) == -1)
        {
            // return eof on error
            return EOF;

        }
        if (f->pos_tag == f->end_tag)
        {
            // return eof on eof
            return EOF;
        }
    }

    unsigned char c = f->cache[f->pos_tag - f->tag];
    ++f->pos_tag;
    return c;
}

// io61_read(f, buf, sz)
//    Read up to `sz` characters from `f` into `buf`. Returns the number of
//    characters read on success; normally this is `sz`. Returns a short
//    count, which might be zero, if the file ended before `sz` characters
//    could be read. Returns -1 if an error occurred before any characters
//    were read.

ssize_t io61_read(io61_file* f, char* buf, size_t sz) {
    size_t n_toread = sz;
    size_t nread = 0;

    while (n_toread)
    {
        if (f->pos_tag == f->end_tag)
        {
            if (io61_fill(f) == -1)
            {
                // read failure
                return -1;
            }
            // reached end of file
            if (f->pos_tag == f->end_tag)
            {
                break;
            }
        }

        size_t n_tocopy = std::min(n_toread, (size_t) f->end_tag - f->pos_tag);
        memcpy(&buf[nread], &f->cache[f->pos_tag - f->tag], n_tocopy);
        f->pos_tag += n_tocopy;
        nread += n_tocopy;
        n_toread -= n_tocopy;
    }

    return nread;
}


// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file* f, int ch) {
    if (f->pos_tag == f->end_tag)
    {
        if(io61_flush(f) == -1)
        {
            // write error
            return -1;
        }
    }
    f->cache[f->pos_tag - f->tag] = ch;
    f->pos_tag++;
    return 0;
}


// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file* f, const char* buf, size_t sz) {
    size_t n_towrite = sz;
    size_t nwritten = 0;

    while (n_towrite)
    {
        if (f->pos_tag == f->end_tag)
        {
            if(io61_flush(f) == -1)
            {
                // write error
                return -1;
            }
        }

        size_t n_tocopy = std::min(n_towrite, (size_t) f->end_tag - f->pos_tag);
        memcpy(&f->cache[f->pos_tag - f->tag], &buf[nwritten], n_tocopy);
        f->pos_tag += n_tocopy;
        nwritten += n_tocopy;
        n_towrite -= n_tocopy;
    }

    return nwritten;
}


// io61_flush(f)
//    Forces a write of all buffered data written to `f`.
//    If `f` was opened read-only, io61_flush(f) may either drop all
//    data buffered for reading, or do nothing.

int io61_flush(io61_file* f) {
    // write from tag -> pos_tag
    int nwritten = write(f->fd, f->cache, f->pos_tag - f->tag);
    if (nwritten >= 0)
    {
        // update tags
        f->tag = f->pos_tag;
        f->end_tag += CACHESIZE;
        return 0;
    }
    return -1;
}


// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file* f, off_t pos) {
    if (f->mode == O_RDONLY)
    {
        int offset = pos % CACHESIZE;
        if (pos >= f->tag && pos < f->end_tag)
        {
            f->pos_tag = pos;
            return 0;
        }

        off_t new_tag = lseek(f->fd, pos - offset, SEEK_SET);
        if (new_tag == -1)
        {
            return -1;
        }

        f->end_tag = new_tag;
        io61_fill(f);
        f->pos_tag += offset;

        return 0;
    }
    else if (f->mode == O_WRONLY)
    {
        io61_flush(f);
        off_t new_tag = lseek(f->fd, pos, SEEK_SET);
        if (new_tag == -1)
        {
            return -1;
        }
        f->pos_tag = f->tag = new_tag;
        f->end_tag = f->tag + CACHESIZE;

        return 0;
    }

    return -1;
}


// You shouldn't need to change these functions.

// io61_open_check(filename, mode)
//    Open the file corresponding to `filename` and return its io61_file.
//    If `!filename`, returns either the standard input or the
//    standard output, depending on `mode`. Exits with an error message if
//    `filename != nullptr` and the named file cannot be opened.

io61_file* io61_open_check(const char* filename, int mode) {
    int fd;
    if (filename) {
        fd = open(filename, mode, 0666);
    } else if ((mode & O_ACCMODE) == O_RDONLY) {
        fd = STDIN_FILENO;
    } else {
        fd = STDOUT_FILENO;
    }
    if (fd < 0) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(1);
    }
    return io61_fdopen(fd, mode & O_ACCMODE);
}


// io61_filesize(f)
//    Return the size of `f` in bytes. Returns -1 if `f` does not have a
//    well-defined size (for instance, if it is a pipe).

off_t io61_filesize(io61_file* f) {
    struct stat s;
    int r = fstat(f->fd, &s);
    if (r >= 0 && S_ISREG(s.st_mode)) {
        return s.st_size;
    } else {
        return -1;
    }
}
