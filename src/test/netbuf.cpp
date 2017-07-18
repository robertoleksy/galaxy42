// Copyrighted (C) 2015-2017 Antinet.org team, see file LICENCE-by-Antinet.txt

#include "gtest/gtest.h"
#include "../netbuf.hpp"

TEST(netbuf, netchunk_ctor) {
	std::array<c_netchunk::t_element, 100> source_array;
	source_array.fill(0x01);

	EXPECT_NO_THROW(c_netchunk netchunk(nullptr, 0U));
	EXPECT_NO_THROW(c_netchunk netchunk(nullptr, 10U));
	EXPECT_NO_THROW(c_netchunk netchunk(source_array.data(), 0U));
	EXPECT_NO_THROW(c_netchunk netchunk(source_array.data(), source_array.size()));

	c_netchunk netchunk(source_array.data(), source_array.size());
	EXPECT_EQ(source_array.data(), netchunk.m_data);
	EXPECT_EQ(source_array.size(), netchunk.m_size);
}

TEST(netbuf, netchunk_move) {
	std::array<c_netchunk::t_element, 100> source_array;
	source_array.fill(0x01);
	c_netchunk netchunk(source_array.data(), source_array.size());
	c_netchunk netchunk2 = std::move(netchunk);
	EXPECT_EQ(netchunk.data(), nullptr);
	EXPECT_EQ(netchunk.size(), 0U);
	EXPECT_EQ(netchunk2.m_data, source_array.data());
	EXPECT_EQ(netchunk2.m_size, source_array.size());
}

TEST(netbuf, netchunk_shrink_to) {
	std::array<c_netchunk::t_element, 100> source_array;
	source_array.fill(0x01);
	c_netchunk netchunk(source_array.data(), source_array.size());
	EXPECT_THROW(netchunk.shrink_to(101U), err_check_prog);
	EXPECT_THROW(netchunk.shrink_to(1000U), err_check_prog);

	EXPECT_NO_THROW(netchunk.shrink_to(100U));
	EXPECT_EQ(netchunk.size(), 100U);

	EXPECT_NO_THROW(netchunk.shrink_to(99U));
	EXPECT_EQ(netchunk.size(), 99U);

	EXPECT_NO_THROW(netchunk.shrink_to(0U));
	EXPECT_EQ(netchunk.size(), 0U);

	EXPECT_THROW(netchunk.shrink_to(1U), err_check_prog);
	EXPECT_THROW(netchunk.shrink_to(100U), err_check_prog);
}

TEST(netbuf, netchunk_size) {
	std::array<c_netchunk::t_element, 100> source_array;
	source_array.fill(0x01);
	c_netchunk netchunk(source_array.data(), source_array.size());
	EXPECT_EQ(netchunk.size(), source_array.size());
	EXPECT_EQ(source_array.size(), netchunk.m_size);

	c_netchunk netchunk2(nullptr, 0);
	EXPECT_EQ(netchunk2.size(), 0U);
	EXPECT_EQ(netchunk2.m_size, 0U);
}

TEST(netbuf, netchunk_data) {
	std::array<c_netchunk::t_element, 100> source_array;
	source_array.fill(0x01);
	c_netchunk netchunk(source_array.data(), source_array.size());
	EXPECT_EQ(netchunk.data(), source_array.data());
}
