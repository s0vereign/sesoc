
#include "Numerov.hpp"

Numerov::Numerov():nx(0),ne(0),xmax(0),xmin(0){}

Numerov::Numerov(Params1D *pa,complex<double>* ps): param(pa),
                                                    chunk(pa->getnx()*CHUNKSIZE),cache(pa->getnx()*CHUNKSIZE),
                                                    nx(pa->getnx()),
                                                    ne(pa->getne()),
                                                    z(pa->getz()),
                                                    xmax(pa->gettmax()),
                                                    xmin(pa->getxmin()) {
    // initialize the cache, with the inital values
    for(auto it = cache.begin(); it != cache.end(); it += nx) {
        // the first entry always has to be zero, the second
        // is an arbitrary small value
        *(it) = 0;
        *(it+1) = -1e-10;
    }
    //get the minimal energy E_n is in [V(0,0,z),0]
    //We'll define it as positive
    //Kernel code then uses negaive values!
    E = V(0,0,z);
}

Numerov::~Numerov(){}

void Numerov::solve(){
    //create a double device ponter
    double* dev_ptr;
    //now we enter a loop
    //which computes a "chunk" of Solutions
    //which gets analyzed and the overwritten
    //after each step
    STATUS("Allocation of graphics memory")
    cudaMalloc( (void**)&dev_ptr, sizeof(double) * nx * CHUNKSIZE);
    ENDSTATUS
    double dE = E / (double) ne;
    auto E_lok = 0.0;
    DEBUG("Initial cache: " <<cache[0])
    for(auto j = 0; j < ne; j += CHUNKSIZE) {
        //push the chunk vector (with initial conditions
        //into the allocated device memory
        E_lok += dE * (double) j;
        DEBUG("Using local energy "<<E_lok)
        cudaMemcpy( dev_ptr, cache.data(), sizeof(double)*CHUNKSIZE*nx, cudaMemcpyHostToDevice);
        STATUS("Calculating the "<<j<<"-th Chunk")
        iter1<<< 256, 4>>>( dev_ptr, nx, CHUNKSIZE, xmax, xmin, z, E_lok, dE);
        cudaMemcpy(chunk.data(), dev_ptr, sizeof(double) * CHUNKSIZE * nx, cudaMemcpyDeviceToHost);
        DEBUG("first value: "<< chunk[0])
        DEBUG("second value: "<< chunk[1])
        DEBUG("third value: "<< chunk[2])
        ENDSTATUS
        STATUS("Running bisection")
        std::cout<<" "<<std::endl;
		//bisect(j);
		ENDSTATUS
    }
    cudaFree(dev_ptr);
    STATUS("Saving Energy Levels")
    savelevels();
    //tempprint();
    ENDSTATUS
}

void Numerov::savelevels(){
    // Function to provide saving functionality of the energy Levels
    // First Allocate an appropiate vector
    hid_t file_id;
    vector<double> buffer1(results.size());
    vector<double> buffer2(eval.size());
    vector<double> buffer3(nx);
    // Save the results into buffer1 vector
    for(auto i = 0; i < results.size();i+=nx){
        // Write the first list elements into a vector
        buffer3 = results.back();
        // Now we write the data into another vector
        // I know this is stupid, but since list has no direct data access
        // I see no other choice.
        for(auto j = 0; j < nx; j++) {
            buffer1[i+j] = buffer3[j];
        }
        // Now pop the first element from the list
        results.pop_back();
    }
    for(auto it = 0; it < eval.size(); it++) {
        // We do the analog thing for the enegy
        // It's a lot simpler!
        buffer2[it] = eval.back();
        eval.pop_back();
    }
    // Create a new HDF5 file

    DEBUG("CALL 1")
    file_id = H5Fcreate("res.h5",H5F_ACC_TRUNC,H5P_DEFAULT,H5P_DEFAULT);
    DEBUG("CALL 2")
    hsize_t dims = buffer1.size();
    // Create a HDF5 Data set and write buffer1
    DEBUG("CALL 3")
    H5LTmake_dataset(file_id, "/numres", 1, &dims, H5T_NATIVE_DOUBLE, buffer1.data());
    // Analog for buffer2
    DEBUG("CALL 4")
    dims = buffer2.size();
    DEBUG("CALL 5")
    H5LTmake_dataset(file_id, "/evals", 1, &dims, H5T_NATIVE_DOUBLE, buffer2.data());
    //Save some necessary parameters
    vector<double> p(2);
    p[0] = nx;
    p[1] = ne;
    dims = 2;
    H5LTmake_dataset( file_id, "/params", 1, &dims, H5T_NATIVE_DOUBLE, p.data());
    // close the file
    H5Fclose(file_id);

 }

bool Numerov::sign(double s){
    return std::signbit(s);
}



void Numerov::bisect(int j) {
    // Iterate through chunk data
    int d;
    for (auto it = chunk.begin() + nx; it != chunk.end(); it += nx) {
        //   DEBUG("Current Psi: " <<  *(it+nx-1))

        if (sign(*(it)) != sign(*(it - nx - 1))) {
            DEBUG("Signchange detected!")
            if (fabs(*(it)) < fabs(*(it - nx - 1)) &&
                (fabs(*(it)) < 1e-3)) {
                DEBUG("Psi0 = " << *(it))
                DEBUG("Psi1 = " << *(it - nx - 1))
                d = std::distance(chunk.begin(), it) / nx;
                vector<double> v(it, (it + nx - 1));
                results.push_back(v);
                DEBUG("First element: " << results.back()[100])
                eval.push_back((double) d * E / (double) ne);
                DEBUG("Energy level found at index = " << d)
            }
            /*else {
                if (fabs(*(it - nx - 1)) < 1e-3) {

                    d = std::distance(chunk.begin(),it - nx - 1)/nx;
                    DEBUG("SEGFAULT ?")
                    vector<double> v(it - 2 * nx - 1, it - nx - 1);
                    results.push_back(v);
                    eval.push_back((double) ( d - 1) * E / (double) ne);
                    DEBUG("Psi0 = " << *(it))
                    DEBUG("Psi1 = "<< *(it - nx - 1) )
                    DEBUG("Energy level found at index = " << d)

                }*/

        }
        else {
            DEBUG("No sign change detected")
        }
    }
}

void Numerov::tempprint(){
    hid_t fileid;
    hsize_t dim=7;
    //Create temporal array for parameter location:
    double* tempdata = (double*) malloc(sizeof(double)*7);
    tempdata[0]=param->getxmax();
    tempdata[1]=param->getxmin();
    tempdata[2]=param->gettmax();
    tempdata[3]=param->gettmin();
    tempdata[4]=(double) param->getnx();
    tempdata[5]=(double) param->getnt();
    tempdata[6]=(double) param->getne();
    //create the hdf5 file:
    fileid = H5Fcreate("res_temp.h5",H5F_ACC_TRUNC,H5P_DEFAULT,H5P_DEFAULT);
    //Print the parameters in the file
    H5LTmake_dataset(fileid,"/params1d",1,&dim, H5T_NATIVE_DOUBLE,tempdata);
    free(tempdata);
    //rewrite dims
    dim = (param->getnx())*(param->getne());
    //print the simulation results in the HDF5 file
    H5LTmake_dataset(fileid, "/numres", 1, &dim, H5T_NATIVE_DOUBLE,chunk.data());
    //now write the energy indices to the file
    //close the file
    H5Fclose(fileid);
    //free memory
    DEBUG("Finished saving!")
};