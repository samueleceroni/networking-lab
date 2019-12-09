# Implementazione superserver

Il funzionamento della nostra implementazione del superserver può essere suddiviso in X(TODO) parti principali:

1. lettura della configurazione
2. ciclo principale
3. gestione dell'avvio dei singoli servizi
4. gestione dei segnali

## Lettura della configurazione
All'avvio del superserver viene letto il file di configurazione chiamato `conf.txt`; durante questa operazione

* le righe vuote o contenenti solamente caratteri di spaziatura vengono ignorate
* i caratteri di spaziatura all'inizio e alla fine di una riga vengono ignorati
* qualsiasi sequenza composta da soli caratteri di spaziatura viene considerata come un singolo spazio

La configurazione viene poi interpretata e salvata in un vettore di tipo `ServiceDataVector` contenente elementi di tipo `ServiceData`:
l'utilizzo di un tipo dedicato per il vettore ci permette di tenere facilmente traccia della sua dimensione in un parametro `size_t size;`.  
Durante l'interpretazione della configurazione vengono fatti alcuni controlli, in particolare:

* il secondo parametro deve essere `tcp` o `udp`
* il terzo parametro deve essere un intero positivo minore di 65536
* il quarto parametro deve essere `wait` o `nowait`

Viene inoltre estratto il nome del servizio dal percorso indicato come primo parametro