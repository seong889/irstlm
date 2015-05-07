/******************************************************************************
 IrstLM: IRST Language Model Toolkit, compile LM
 Copyright (C) 2006 Marcello Federico, ITC-irst Trento, Italy
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 
 **********************************************dou********************************/

#include <sys/mman.h>
#include <stdio.h>
#include <cmath>
#include <string>
#include <sstream>
#include <pthread.h>
#include "thpool.h"
#include "mfstream.h"
#include "mempool.h"
#include "htable.h"
#include "n_gram.h"
#include "util.h"
#include "dictionary.h"
#include "ngramtable.h"
#include "doc.h"
#include "cplsa.h"



using namespace std;

#define MY_RAND (((float)random()/RAND_MAX)* 2.0 - 1.0)
	
plsa::plsa(dictionary* d,int top,char* wd,bool mm){
    
    dict=d;

    topics=top;
    
    tmpdir=wd;
    
    memorymap=mm;
    
    MY_ASSERT (topics>0);
    
    //actual model structure
    W=NULL;
    
    //training support structure
    T=NULL;
    
    //allocate/free at training time// this is the huge table
    H=NULL;
    
    
    srandom(100); //consistent generation of random noise
    
}

plsa::~plsa() {
    freeW();
    freeH();
    free(T);
}
	
int plsa::initW(char* modelfile,float noise,int spectopic){
    
    //W needs a dictionary, either from an existing model or
    //from the training data
    
    assert(W==NULL);
    
    if (dict==NULL) loadW(modelfile);
    else{
        cerr << "Allocating W table\n";
        W=new float* [dict->size()];
        for (int i=0; i<dict->size(); i++){
            W[i]=new float [topics](); //initialized to zero since C++11
            memset(W[i],0,sizeof(float)*topics);
        }
        cerr << "Initializing W table\n";
        if (spectopic) {
            //special topic 0: first st most frequent
            //assume dictionary is sorted by frequency!!!
            float TotW=0;
            for (int i=0; i<spectopic; i++)
                TotW+=W[i][0]=dict->freq(i);
            for (int i=0; i<spectopic; i++)
                W[i][0]/=TotW;
        }
        
        for (int t=(spectopic?1:0); t<topics; t++) {
            float TotW=0;
            for (int i=spectopic; i< dict->size(); i++)
                TotW+=W[i][t]=1 + noise * MY_RAND;
            for (int i=spectopic; i< dict->size(); i++)
                W[i][t]/=TotW;
        }
    }
    return 1;
}

int plsa::freeW(){
    if (W!=NULL){
        cerr << "Releasing memory of W table\n";
        for (int i=0; i<dict->size(); i++) delete [] W[i];
        delete [] W;
        W=NULL;
    }
    return 1;
}



int plsa::initH(){
    
    assert(trset->numdoc()); //need a date set
    long long len=(unsigned long long)trset->numdoc() * topics;
   
    FILE *fd;
    if (H == NULL){
        if (memorymap){
            cerr << "Creating memory mapped H table\n";
            //generate a name for the memory map file
            sprintf(Hfname,"/%s/Hfname%d",tmpdir,(int)getpid());
            if ((fd=fopen(Hfname,"w+"))==0){
                perror("Could not create file");
                exit_error(IRSTLM_ERROR_IO, "plsa::initH fopen error");
            }
            //H is aligned at integer
            ftruncate(fileno(fd),len * sizeof(float));
            H = (float *)mmap( 0, len * sizeof(float) , PROT_READ|PROT_WRITE, MAP_PRIVATE,fileno(fd),0);
            fclose(fd);
            if (H == MAP_FAILED){
                perror("Mmap error");
                exit_error(IRSTLM_ERROR_IO, "plsa::initH MMAP error");
            }
        }
        else{
            cerr << "Allocating " << len << " entries for H table\n";
            fprintf(stderr,"%llu\n",len);
            if ((H=new float[len])==NULL){
                perror("memory allocation error");
                exit_error(IRSTLM_ERROR_IO, "plsa::cannot allocate memory for H");
            }
        }
    }
    cerr << "Initializing H table " << getpid() << "\n";
    float value=1/(float)topics;
    for (long long d=0; d< trset->numdoc(); d++)
        for (int t=0; t<topics; t++)
            H[d*topics+t]=value;
    cerr << "done\n";
    return 1;
}

int plsa::freeH(){
    if (H!=NULL){
        cerr << "Releasing memory for H table\n";
        if (memorymap){
            munmap((void *)H,trset->numdoc()*topics*sizeof(float));
            remove(Hfname);
        }else
            delete [] H;
        
        H=NULL;

    }
    return 1;
}


int plsa::initT(){ //keep double for counts collected over the whole training data
    if (T==NULL){
        T=new double* [dict->size()];
        for (int i=0; i<dict->size(); i++)
            T[i]=new double [topics];
    }
    for (int i=0; i<dict->size(); i++)
        memset((void *)T[i],0,topics * sizeof(double));
    
    return 1;
}

int plsa::freeT(){
    if (T!=NULL){
        cerr << "Releasing memory for T table\n";
        for (int i=0; i<dict->size(); i++) delete [] T[i];
        delete [] T;
        T=NULL;
    }
    return 1;
}



int plsa::saveWtxt(char* fname){
    cerr << "Writing text W table into: " << fname << "\n";
    mfstream out(fname,ios::out);
    out.precision(5);
//  out << topics << "\n";
    for (int i=0; i<dict->size(); i++) {
        out << dict->decode(i);// << " " << dict->freq(i);
        //double totW=0;
        //for (int t=0; t<topics; t++) totW+=W[i][t];
        //out <<" totPr: " << totW << " :";
        for (int t=0; t<topics; t++)
            out << " " << W[i][t];
        out << "\n";
    }
    out.close();
    return 1;
}
	

int plsa::saveW(char* fname){
    cerr << "Saving model into: " << fname << " ...";
    mfstream out(fname,ios::out);
    out << "PLSA " << topics << "\n";
    dict->save(out);
    for (int i=0; i<dict->size(); i++)
        out.write((const char*)W[i],sizeof(float) * topics);
    out.close();
    cerr << "\n";
    return 1;
}

int plsa::loadW(char* fname){
    assert(dict==NULL);
    cerr << "Loading model from: " << fname << "\n";
    mfstream inp(fname,ios::in);
    char header[100];
    inp.getline(header,100);
    cerr << header ;
    int r;
    sscanf(header,"PLSA %d\n",&r);
    if (topics>0 && r != topics)
        exit_error(IRSTLM_ERROR_DATA, "incompatible number of topics");
    else
        topics=r;

    cerr << "Loading dictionary\n";
    dict=new dictionary(NULL,1000000);
    dict->load(inp);
    dict->encode(dict->OOV());
    cerr << "Allocating W table\n";
    W=new float* [dict->size()];
    for (int i=0; i<dict->size(); i++)
        W[i]=new float [topics];

    cerr << "Reading W table .... ";
    for (int i=0; i<dict->size(); i++)
        inp.read((char *)W[i],sizeof(float) * topics);

    inp.close();
    cerr << "\n";
    return 1;
}

int plsa::saveWordFeatures(char* fname){
    
    //extend this to save features for all adapation documents
    //compute distribution on doc 0
    assert(trset !=NULL);
    
    cerr << "Saving word features in " << fname << "\n";
    
    double *WH=new double [dict->size()];
    char *outfname=new char[strlen(fname)+10];
    
    if (trset->numdoc()>100)
        cerr << "Will only save first 100 features\n";
    
    for (long long d=0; d < trset->numdoc();d++){
        
        if (d>=100) break;
        
        for (int i=0; i<dict->size(); i++) {
            WH[i]=0;
            for (int t=0; t<topics; t++)
                WH[i]+=W[i][t]*H[d * topics + t];
        }
        
        double maxp=WH[0];
        for (int i=1; i<dict->size(); i++)
            if (WH[i]>maxp) maxp=WH[i];
        
        cerr << "Get max prob" << maxp << "\n";
        
        sprintf(outfname,"%s.%03d",fname,(int)d+1);
        {   //save unigrams in google ngram format
            mfstream out(outfname,ios::out);
            for (int i=0; i<dict->size(); i++){
                int freq=(int)floor((WH[i]/maxp) * 1000000);
                if (freq)
                    out << dict->decode(i) <<" \t" << freq<<"\n";
                
            }
            out.close();
        }
  
    }
    
    delete [] outfname;
    delete [] WH;
    return 1;
}


///*****
pthread_mutex_t mut1;
pthread_mutex_t mut2;
double LL=0; //Log likelihood
const float topicthreshold=0.00001;

void plsa::expected_counts(void *argv){
    
    long long d;
    d=(long long) argv;
    
    if (! (d % 10000)) {cerr << ".";cerr.flush();}
    //fprintf(stderr,"Thread: %lu  Document: %d  (out of %d)\n",(long)pthread_self(),d,trset->numdoc());
    
    int r=topics;
    
    
    int m=trset->doclen(d); //actual length of document
    int N=m ; // doc length is the same of
    double totH=0;
    
    for (int t=0; t<r; t++) if (H[d * r + t] < topicthreshold) H[d * r + t]=0;
    
    
    //precompute WHij i=0,...,m-1; j fixed
    float *WH=new float [m]; //initialized to zero
    memset(WH,0,sizeof(float)*m);
    for (int t=0; t< r ; t++)
        if (H[d * r + t]>0)
            for (int i=0; i<m; i++) //count each word indipendently!!!!
                WH[i]+=(W[trset->docword(d,i)][t] * H[d * r + t]);
    
    
    //UPDATE LOCAL Tia (for each word and topic)
    //seems more efficient perform local computation on complex structures
    //and perform exclusive computations on simpler structures.
    float *lT=new float[m * r];
    memset(lT,0,sizeof(float)*m*r);
    for (int t=0; t<r; t++)
        if (H[d * r + t]>0)
            for (int i=0; i<m; i++)
                lT[i * r + t]=(W[trset->docword(d,i)][t] * H[d * r + t]/WH[i]);

    //UPDATE GLOBAL T and LL
    pthread_mutex_lock(&mut1);
    for (int i=0; i<m; i++){
        for (int t=0; t<r; t++)
                T[trset->docword(d,i)][t]+=(double)lT[i * r + t];
        LL+= log( WH[i] );
    }
    pthread_mutex_unlock(&mut1);


    //UPDATE Haj (topic a and document j)
    totH=0;
    for (int t=0; t<r; t++){
        float tmpHaj=0;
        if (H[d * r + t]>0){
            for (int i=0; i < m; i++)
                tmpHaj+=(W[trset->docword(d,i)][t] * H[d * r + t]/WH[i]);
            H[d * r + t]=tmpHaj/N;
            totH+=H[d * r + t];
        }
    }
    
    if(totH>UPPER_SINGLE_PRECISION_OF_1 || totH<LOWER_SINGLE_PRECISION_OF_1){
        std::stringstream ss_msg;
        ss_msg << "Total H is wrong; totH=" << totH << " ( doc= " << d << ")\n";
        exit_error(IRSTLM_ERROR_MODEL, ss_msg.str());
    }
    
    delete [] WH;
    delete [] lT;
    
};



int plsa::train(char *trainfile, char *modelfile, int maxiter,int threads,float noiseW,int spectopic){
    

    //check if to either use the dict of the modelfile
    //or create a new one from the data
    //load training data!
    
    
    //Initialize W matrix and load training data
    //notice: if dict is empy, then upload from model
    initW(modelfile,noiseW,spectopic);

    //Load training data
    trset=new doc(dict,trainfile);

    //allocate H table
    initH();
    
    
    int iter=0;
    int r=topics;
    
    threadpool thpool=thpool_init(threads);
    task *t=new task[trset->numdoc()];
    
    pthread_mutex_init(&mut1, NULL);
    //pthread_mutex_init(&mut2, NULL);
    
    while (iter < maxiter){
        LL=0;
        
        //initialize T table
        initT();
        
        for (long long d=0;d<trset->numdoc();d++){
            //prepare and assign tasks to threads
            t[d].ctx=this; t[d].argv=(void *)d;
            thpool_add_work(thpool, &plsa::expected_counts_helper, (void *)&t[d]);
            
        }
        //join all threads
        thpool_wait(thpool);
        
        //Recombination and normalization of expected counts
        for (int t=0; t<r; t++) {
            double Tsum=0;
            for (int i=0; i<dict->size(); i++) Tsum+=T[i][t];
            for (int i=0; i<dict->size(); i++) W[i][t]=(float)(T[i][t]/Tsum);
        }
        
        
        cerr << "Iteration: " << ++iter << " LL: " << LL << "\n";
        if (trset->numdoc()> 10) system("date");
        
        saveW(modelfile);
        
    }
    
    //destroy thread pool
    thpool_destroy(thpool);
  
    
    freeH(); freeT(); freeW();
    
    delete trset;
    delete [] t;
    
    return 1;
}



int plsa::inference(char *testfile, char* modelfile, int maxiter, char* topicfeatfile,char* wordfeatfile){
    
    
    const float deltathreshold=0.001;
    const float topicthreshold=0.0001;
    
    
    //load existing model
    initW(modelfile,0,0);
    
    //load existing model
    trset=new doc(dict,testfile);
    
    //use one vector H for all document
    H=new float[topics];
    
    //support arrays
    int dsize=dict->size(); //includes possible OOV
    
    float *WH=new float [dsize];
    bool   *Hflags=new bool[topics];

    cerr << "start inference\n";
    
    for (long long d=0;d<trset->numdoc();d++){
        
        int M=trset->doclen(d); //vocabulary size of current documents with repetitions
        
        int N=M;  //document length
        
        //initialize H: we estimate one H for each document
        for (int t=0; t<topics; t++) {H[t]=1/(float)topics;Hflags[t]=true;}
        
        int iter=0;
        
        double LL=0;
        float delta=0;
        float maxdelta=1;
        
        while (iter <= maxiter && maxdelta > deltathreshold){
            
            maxdelta=0;
            iter++;
            
            //precompute denominator WH
            for (int t=0; t<topics; t++)
                if (Hflags[t] && H[t] < topicthreshold){ Hflags[t]=false; H[t]=0;}

            for (int i=0; i < M ; i++) {
                WH[trset->docword(d,i)]=0; //initialized
                for (int t=0; t<topics; t++){
                    if (Hflags[t])
                    WH[trset->docword(d,i)]+=W[trset->docword(d,i)][t] * H[t];
                }
                LL-= log( WH[trset->docword(d,i)] );
            }
            
            //cerr << "LL: " << LL << "\n";
            
            //UPDATE H
            float totH=0;
            for (int t=0; t<topics; t++) {
                if (Hflags[t]){
                    float tmpH=0;
                    for (int i=0; i< M ; i++)
                        tmpH+=(W[trset->docword(d,i)][t] * H[t]/WH[trset->docword(d,i)]);
                    delta=abs(H[t]-tmpH/N);
                    if (delta > maxdelta) maxdelta=delta;
                    H[t]=tmpH/N;
                    totH+=H[t]; //to check that sum is 1
                }
            }
            
            if(totH>UPPER_SINGLE_PRECISION_OF_1 || totH<LOWER_SINGLE_PRECISION_OF_1) {
                cerr << "totH " << totH << "\n";
                std::stringstream ss_msg;
                ss_msg << "Total H is wrong; totH=" << totH << "\n";
                exit_error(IRSTLM_ERROR_MODEL, ss_msg.str());
            }
            
        }
        cerr << "Stopped at iteration " << iter << "\n";
        
        if (topicfeatfile){
            mfstream out(topicfeatfile,ios::out| ios::app);
            out << H[0];
            for (int t=1; t<topics; t++) out << " "  << H[t];
            out << "\n";
        }
        if (wordfeatfile)
            saveWordFeatures(wordfeatfile);
    }
    
    delete [] WH; delete [] Hflags; delete []H;
    delete trset;
    return 1;
}

