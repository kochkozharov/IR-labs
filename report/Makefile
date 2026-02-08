all: report.pdf

report.pdf: report.tex src/*.tex
	pdflatex report.tex
	pdflatex report.tex

clean:
	rm -f *.aux *.log *.out *.toc *.pdf

.PHONY: all clean
