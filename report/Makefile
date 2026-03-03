all: report.pdf

report.pdf: report.tex src/*.tex
	pdflatex report.tex
	pdflatex report.tex
	mv report.pdf М8О-408Б-22_Кочкожаров_Иван_Вячеславович.pdf

clean:
	rm -f *.aux *.log *.out *.toc *.pdf

.PHONY: all clean
