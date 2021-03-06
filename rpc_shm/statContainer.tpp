#include <algorithm>
#include <iterator>
#include <vector>
#include <iostream>
#include <fstream>

template <typename T>
void StatsContainer<T>::append(T& appendMe)
{
    underlyingContainer.push_back(appendMe); // vector is easy
}

template <typename T>
T& StatsContainer<T>::get(size_t idx)
{
    return underlyingContainer.at(idx);
}

template <typename T>
void StatsContainer<T>::writeToFile(std::string fname)
{
    std::ofstream fout(fname, std::ios::out | std::ios::binary);
    for( size_t i = 0; i < underlyingContainer.size(); ++i) {
        fout.write((char*) &(underlyingContainer[i]), sizeof(T));
    }
    /*
    std::copy( underlyingContainer.begin(), underlyingContainer.end(),
               std::ostreambuf_iterator<char>(fout) );
    */
}

template <typename T>
void StatsContainer<T>::readFromFile(std::string fname,size_t num_elements)
{
    underlyingContainer.resize(num_elements); // allocate once
    std::ifstream fin(fname, std::ios::in | std::ios::binary);
    //std::istreambuf_iterator<char> iter(fin);
    //std::istreambuf_iterator<char> eos_special;
    for( size_t i = 0; i < underlyingContainer.size(); ++i) {
        fin.read((char*)&(underlyingContainer[i]), sizeof(T));
    }
    /*
    while( iter != eos_special && ( elementsScanned++ < num_elements ) ) {
        underlyingContainer.push_back( ) ;
    }
    */
    //std::copy( iter, eos_special, std::back_inserter(underlyingContainer) );
}
