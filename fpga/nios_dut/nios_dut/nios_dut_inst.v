	nios_dut u0 (
		.audio_in_data            (<connected-to-audio_in_data>),            //         audio_in.data
		.audio_in_valid           (<connected-to-audio_in_valid>),           //                 .valid
		.audio_in_ready           (<connected-to-audio_in_ready>),           //                 .ready
		.audio_out_data           (<connected-to-audio_out_data>),           //        audio_out.data
		.audio_out_valid          (<connected-to-audio_out_valid>),          //                 .valid
		.audio_out_ready          (<connected-to-audio_out_ready>),          //                 .ready
		.dummy_export             (<connected-to-dummy_export>),             //            dummy.export
		.io_ack                   (<connected-to-io_ack>),                   //               io.ack
		.io_rdata                 (<connected-to-io_rdata>),                 //                 .rdata
		.io_read                  (<connected-to-io_read>),                  //                 .read
		.io_wdata                 (<connected-to-io_wdata>),                 //                 .wdata
		.io_write                 (<connected-to-io_write>),                 //                 .write
		.io_address               (<connected-to-io_address>),               //                 .address
		.io_irq                   (<connected-to-io_irq>),                   //                 .irq
		.io_u2p_ack               (<connected-to-io_u2p_ack>),               //           io_u2p.ack
		.io_u2p_rdata             (<connected-to-io_u2p_rdata>),             //                 .rdata
		.io_u2p_read              (<connected-to-io_u2p_read>),              //                 .read
		.io_u2p_wdata             (<connected-to-io_u2p_wdata>),             //                 .wdata
		.io_u2p_write             (<connected-to-io_u2p_write>),             //                 .write
		.io_u2p_address           (<connected-to-io_u2p_address>),           //                 .address
		.io_u2p_irq               (<connected-to-io_u2p_irq>),               //                 .irq
		.jtag_io_input_vector     (<connected-to-jtag_io_input_vector>),     //          jtag_io.input_vector
		.jtag_io_output_vector    (<connected-to-jtag_io_output_vector>),    //                 .output_vector
		.jtag_test_clocks_clock_1 (<connected-to-jtag_test_clocks_clock_1>), // jtag_test_clocks.clock_1
		.jtag_test_clocks_clock_2 (<connected-to-jtag_test_clocks_clock_2>), //                 .clock_2
		.mem_mem_req_address      (<connected-to-mem_mem_req_address>),      //              mem.mem_req_address
		.mem_mem_req_byte_en      (<connected-to-mem_mem_req_byte_en>),      //                 .mem_req_byte_en
		.mem_mem_req_read_writen  (<connected-to-mem_mem_req_read_writen>),  //                 .mem_req_read_writen
		.mem_mem_req_request      (<connected-to-mem_mem_req_request>),      //                 .mem_req_request
		.mem_mem_req_tag          (<connected-to-mem_mem_req_tag>),          //                 .mem_req_tag
		.mem_mem_req_wdata        (<connected-to-mem_mem_req_wdata>),        //                 .mem_req_wdata
		.mem_mem_resp_dack_tag    (<connected-to-mem_mem_resp_dack_tag>),    //                 .mem_resp_dack_tag
		.mem_mem_resp_data        (<connected-to-mem_mem_resp_data>),        //                 .mem_resp_data
		.mem_mem_resp_rack_tag    (<connected-to-mem_mem_resp_rack_tag>),    //                 .mem_resp_rack_tag
		.pio1_export              (<connected-to-pio1_export>),              //             pio1.export
		.pio2_export              (<connected-to-pio2_export>),              //             pio2.export
		.pio3_export              (<connected-to-pio3_export>),              //             pio3.export
		.sys_clock_clk            (<connected-to-sys_clock_clk>),            //        sys_clock.clk
		.sys_reset_reset_n        (<connected-to-sys_reset_reset_n>),        //        sys_reset.reset_n
		.uart_rxd                 (<connected-to-uart_rxd>),                 //             uart.rxd
		.uart_txd                 (<connected-to-uart_txd>),                 //                 .txd
		.uart_cts_n               (<connected-to-uart_cts_n>),               //                 .cts_n
		.uart_rts_n               (<connected-to-uart_rts_n>)                //                 .rts_n
	);

