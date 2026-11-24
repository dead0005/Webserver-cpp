cmake_minimum_required(VERSION 3.16)

project(CppSelectServer LANGUAGES CXX)

if(NOT WIN32)
	message(FATAL_ERROR "CppSelectServer currently requires Windows and Winsock2.")
endif()

add_executable(server
	main.cpp
	TcpListener.cpp
	WebServer.cpp
	Router.cpp
	Payment.cpp
	PaymentStore.cpp
	PaymentHandler.cpp
	Ledger.cpp
	ProviderSimulator.cpp
	PersistenceManager.cpp
	Reconciliation.cpp
)

target_compile_features(server PRIVATE cxx_std_14)
target_link_libraries(server PRIVATE ws2_32)

if(MSVC)
	target_compile_options(server PRIVATE /W4 /permissive-)
else()
	target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
