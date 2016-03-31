NAME	=  papillon
LDLIBS	+= -lm -lasound -lfftw3

all: $(NAME)

clean:
	rm -f $(NAME).o $(NAME)

$(NAME): $(NAME).o
